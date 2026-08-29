#include "TimeKeeper.h"

#include <sys/time.h>
#include <time.h>

#include <esp_timer.h>

#include "AppConfig.h"
#include "ClockCore.h"

bool TimeKeeper::begin() {
  preferences_.begin("kids-clock", false);
  uint8_t record[clockcore::kPersistedSyncRecordSize] = {};
  const bool recordPresent = preferences_.isKey("sync-state");
  const size_t recordLength =
      recordPresent ? preferences_.getBytesLength("sync-state") : 0;
  if (recordPresent && recordLength == sizeof(record) &&
      preferences_.getBytes("sync-state", record, sizeof(record)) ==
          sizeof(record)) {
    hasConfirmedSync_ = clockcore::decodePersistedSyncState(
        record, sizeof(record), lastSyncUtc_, syncRoute_, utcOffsetMinutes_);
  } else if (!recordPresent) {
    // Migrate the original four-key layout once. A present but invalid blob
    // fails closed instead of resurrecting a possibly stale legacy tuple.
    const int64_t legacyLastSync = preferences_.getLong64("last-sync", 0);
    const uint8_t legacyRoute = preferences_.getUChar("sync-source", 0);
    const int16_t legacyOffset = preferences_.getShort(
        "utc-offset", config::kDefaultUtcOffsetMinutes);
    hasConfirmedSync_ = clockcore::isValidPersistedSyncState(
        preferences_.getBool("confirmed", false), legacyLastSync,
        legacyRoute, legacyOffset);
    if (hasConfirmedSync_) {
      lastSyncUtc_ = legacyLastSync;
      syncRoute_ = static_cast<SyncRoute>(legacyRoute);
      utcOffsetMinutes_ = legacyOffset;
      if (clockcore::encodePersistedSyncState(
              lastSyncUtc_, syncRoute_, utcOffsetMinutes_, record,
              sizeof(record)) &&
          preferences_.putBytes("sync-state", record, sizeof(record)) ==
              sizeof(record)) {
        preferences_.remove("last-sync");
        preferences_.remove("sync-source");
        preferences_.remove("utc-offset");
        preferences_.remove("confirmed");
      }
    }
  }
  if (!hasConfirmedSync_) {
    lastSyncUtc_ = 0;
    syncRoute_ = SyncRoute::kUnselected;
    utcOffsetMinutes_ = config::kDefaultUtcOffsetMinutes;
  }

  rtcAvailable_ = rtc_.begin();
  if (!rtcAvailable_ || rtc_.lostPower()) {
    return false;
  }

  const DateTime rtcTime = rtc_.now();
  const int64_t epoch = rtcTime.unixtime();
  if (!clockcore::isValidEpoch(epoch)) {
    return false;
  }
  const timeval tv = {static_cast<time_t>(epoch), 0};
  settimeofday(&tv, nullptr);
  correctionBaselineUtc_ = epoch;
  correctionBaselineUs_ = static_cast<uint64_t>(esp_timer_get_time());
  correctionBaselineValid_ = true;
  return true;
}

bool TimeKeeper::apply(const TimeUpdate& update) {
  if (!clockcore::isValidEpoch(update.unixUtc) ||
      !clockcore::isValidUtcOffset(update.utcOffsetMinutes)) {
    return false;
  }
  const int64_t trustedNow =
      correctionBaselineValid_
          ? clockcore::extrapolateMonotonicEpoch(
                correctionBaselineUtc_, correctionBaselineUs_,
                static_cast<uint64_t>(esp_timer_get_time()))
          : 0;
  if (!clockcore::isAcceptableCorrection(
          hasConfirmedSync_ && clockcore::isValidEpoch(trustedNow),
          trustedNow, update.unixUtc)) {
    return false;
  }

  const timeval tv = {static_cast<time_t>(update.unixUtc), 0};
  if (settimeofday(&tv, nullptr) != 0) {
    return false;
  }
  correctionBaselineUtc_ = update.unixUtc;
  correctionBaselineUs_ = static_cast<uint64_t>(esp_timer_get_time());
  correctionBaselineValid_ = true;
  utcOffsetMinutes_ = update.utcOffsetMinutes;
  timezoneFresh_ =
      update.source == TimeSource::kBle || update.source == TimeSource::kPortal;
  const SyncRoute candidateRoute =
      clockcore::syncRouteForSource(update.source);
  if (candidateRoute == SyncRoute::kBle ||
      syncRoute_ == SyncRoute::kUnselected) {
    syncRoute_ = candidateRoute;
  }
  lastSyncUtc_ = update.unixUtc;
  if (rtcAvailable_) {
    // Update the optional RTC before committing confirmed trust. If power is
    // lost during the I2C write, the old atomic record remains and the next
    // boot cannot pair new trust with an old RTC baseline.
    rtc_.adjust(DateTime(static_cast<uint32_t>(update.unixUtc)));
  }
  uint8_t record[clockcore::kPersistedSyncRecordSize] = {};
  if (clockcore::encodePersistedSyncState(
          lastSyncUtc_, syncRoute_, utcOffsetMinutes_, record,
          sizeof(record)) &&
      preferences_.putBytes("sync-state", record, sizeof(record)) ==
          sizeof(record)) {
    // Remove the old tuple after the new record commits. This prevents a
    // later erased or corrupt blob from reviving stale legacy trust.
    preferences_.remove("last-sync");
    preferences_.remove("sync-source");
    preferences_.remove("utc-offset");
    preferences_.remove("confirmed");
  }
  hasConfirmedSync_ = true;
  return true;
}

void TimeKeeper::clearSyncTrust() {
  preferences_.clear();
  lastSyncUtc_ = 0;
  utcOffsetMinutes_ = config::kDefaultUtcOffsetMinutes;
  syncRoute_ = SyncRoute::kUnselected;
  timezoneFresh_ = false;
  hasConfirmedSync_ = false;
  correctionBaselineUtc_ = 0;
  correctionBaselineUs_ = 0;
  correctionBaselineValid_ = false;
}

bool TimeKeeper::hasValidTime() const {
  return clockcore::isValidEpoch(utcNow());
}

int64_t TimeKeeper::utcNow() const {
  if (correctionBaselineValid_) {
    const int64_t trustedNow = clockcore::extrapolateMonotonicEpoch(
        correctionBaselineUtc_, correctionBaselineUs_,
        static_cast<uint64_t>(esp_timer_get_time()));
    if (clockcore::isValidEpoch(trustedNow)) {
      return trustedNow;
    }
  }
  // SNTP updates the process clock before its callback is validated. Do not
  // expose that value until an update passes TimeKeeper::apply().
  return 0;
}

void TimeKeeper::localTime(struct tm& result) const {
  const time_t localEpoch = static_cast<time_t>(
      utcNow() + static_cast<int64_t>(utcOffsetMinutes_) * 60LL);
  gmtime_r(&localEpoch, &result);
}
