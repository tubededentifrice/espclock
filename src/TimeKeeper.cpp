#include "TimeKeeper.h"

#include <sys/time.h>
#include <time.h>

#include "AppConfig.h"
#include "ClockCore.h"

bool TimeKeeper::begin() {
  preferences_.begin("kids-clock", false);
  const int64_t lastSync = preferences_.getLong64("last-sync", 0);
  hasConfirmedSync_ =
      preferences_.getBool("confirmed", false) &&
      clockcore::isValidEpoch(lastSync);
  utcOffsetMinutes_ = preferences_.getShort(
      "utc-offset", config::kDefaultUtcOffsetMinutes);
  if (!clockcore::isValidUtcOffset(utcOffsetMinutes_)) {
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
  return true;
}

bool TimeKeeper::apply(const TimeUpdate& update) {
  if (!clockcore::isValidEpoch(update.unixUtc) ||
      !clockcore::isValidUtcOffset(update.utcOffsetMinutes)) {
    return false;
  }
  if (!clockcore::isAcceptableCorrection(
          hasConfirmedSync_, utcNow(), update.unixUtc)) {
    return false;
  }

  const timeval tv = {static_cast<time_t>(update.unixUtc), 0};
  if (settimeofday(&tv, nullptr) != 0) {
    return false;
  }
  utcOffsetMinutes_ = update.utcOffsetMinutes;
  timezoneFresh_ =
      update.source == TimeSource::kBle || update.source == TimeSource::kPortal;
  preferences_.putShort("utc-offset", utcOffsetMinutes_);
  preferences_.putLong64("last-sync", update.unixUtc);
  preferences_.putUChar("sync-source", static_cast<uint8_t>(update.source));
  preferences_.putBool("confirmed", true);
  hasConfirmedSync_ = true;

  if (rtcAvailable_) {
    rtc_.adjust(DateTime(static_cast<uint32_t>(update.unixUtc)));
  }
  return true;
}

void TimeKeeper::clearSyncTrust() {
  preferences_.clear();
  utcOffsetMinutes_ = config::kDefaultUtcOffsetMinutes;
  timezoneFresh_ = false;
  hasConfirmedSync_ = false;
}

bool TimeKeeper::hasValidTime() const {
  return clockcore::isValidEpoch(utcNow());
}

int64_t TimeKeeper::utcNow() const {
  return static_cast<int64_t>(time(nullptr));
}

void TimeKeeper::localTime(struct tm& result) const {
  const time_t localEpoch = static_cast<time_t>(
      utcNow() + static_cast<int64_t>(utcOffsetMinutes_) * 60LL);
  gmtime_r(&localEpoch, &result);
}
