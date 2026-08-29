#include "ClockCore.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace clockcore {

bool isValidEpoch(const int64_t epoch) {
  return epoch >= kMinimumValidEpoch && epoch <= kMaximumValidEpoch;
}

bool isValidUtcOffset(const int offsetMinutes) {
  return offsetMinutes >= -14 * 60 && offsetMinutes <= 14 * 60;
}

bool isPlausibleCorrection(const int64_t currentEpoch,
                           const int64_t candidateEpoch) {
  if (!isValidEpoch(candidateEpoch)) {
    return false;
  }
  if (!isValidEpoch(currentEpoch)) {
    return true;
  }
  const int64_t difference = candidateEpoch - currentEpoch;
  return difference >= -300 && difference <= 300;
}

bool isAcceptableCorrection(const bool hasConfirmedSync,
                            const int64_t currentEpoch,
                            const int64_t candidateEpoch) {
  return isValidEpoch(candidateEpoch) &&
         (!hasConfirmedSync ||
          isPlausibleCorrection(currentEpoch, candidateEpoch));
}

bool isValidSyncRouteValue(const uint8_t value) {
  return value >= static_cast<uint8_t>(SyncRoute::kBle) &&
         value <= static_cast<uint8_t>(SyncRoute::kNtp);
}

bool isValidPersistedSyncState(const bool confirmed,
                               const int64_t lastSyncEpoch,
                               const uint8_t routeValue,
                               const int utcOffsetMinutes) {
  return confirmed && isValidEpoch(lastSyncEpoch) &&
         isValidSyncRouteValue(routeValue) &&
         isValidUtcOffset(utcOffsetMinutes);
}

namespace {
uint32_t syncRecordChecksum(const uint8_t* data, const size_t length) {
  uint32_t checksum = 2166136261UL;
  for (size_t index = 0; index < length; ++index) {
    checksum ^= data[index];
    checksum *= 16777619UL;
  }
  return checksum;
}
}  // namespace

bool encodePersistedSyncState(const int64_t lastSyncEpoch,
                              const SyncRoute route,
                              const int16_t utcOffsetMinutes,
                              uint8_t* output, const size_t outputSize) {
  if (output == nullptr || outputSize != kPersistedSyncRecordSize ||
      !isValidPersistedSyncState(true, lastSyncEpoch,
                                 static_cast<uint8_t>(route),
                                 utcOffsetMinutes)) {
    return false;
  }
  memset(output, 0, outputSize);
  output[0] = 1;
  output[1] = static_cast<uint8_t>(route);
  const uint16_t rawOffset = static_cast<uint16_t>(utcOffsetMinutes);
  output[2] = static_cast<uint8_t>(rawOffset);
  output[3] = static_cast<uint8_t>(rawOffset >> 8U);
  const uint64_t rawEpoch = static_cast<uint64_t>(lastSyncEpoch);
  for (uint8_t index = 0; index < 8; ++index) {
    output[4 + index] =
        static_cast<uint8_t>(rawEpoch >> (8U * index));
  }
  const uint32_t checksum = syncRecordChecksum(output, 12);
  for (uint8_t index = 0; index < 4; ++index) {
    output[12 + index] =
        static_cast<uint8_t>(checksum >> (8U * index));
  }
  return true;
}

bool decodePersistedSyncState(const uint8_t* data, const size_t length,
                              int64_t& lastSyncEpoch, SyncRoute& route,
                              int16_t& utcOffsetMinutes) {
  if (data == nullptr || length != kPersistedSyncRecordSize || data[0] != 1) {
    return false;
  }
  uint32_t storedChecksum = 0;
  for (uint8_t index = 0; index < 4; ++index) {
    storedChecksum |= static_cast<uint32_t>(data[12 + index])
                      << (8U * index);
  }
  if (storedChecksum != syncRecordChecksum(data, 12)) {
    return false;
  }
  uint64_t rawEpoch = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    rawEpoch |= static_cast<uint64_t>(data[4 + index]) << (8U * index);
  }
  const uint16_t rawOffset = static_cast<uint16_t>(data[2]) |
                             (static_cast<uint16_t>(data[3]) << 8U);
  const int64_t candidateEpoch = static_cast<int64_t>(rawEpoch);
  const SyncRoute candidateRoute = static_cast<SyncRoute>(data[1]);
  const int16_t candidateOffset = static_cast<int16_t>(rawOffset);
  if (!isValidPersistedSyncState(true, candidateEpoch, data[1],
                                 candidateOffset)) {
    return false;
  }
  lastSyncEpoch = candidateEpoch;
  route = candidateRoute;
  utcOffsetMinutes = candidateOffset;
  return true;
}

namespace {
uint8_t timeSourcePriority(const TimeSource source) {
  switch (source) {
    case TimeSource::kBle:
      return 3U;
    case TimeSource::kPortal:
      return 2U;
    case TimeSource::kNtp:
      return 1U;
    case TimeSource::kRtc:
    default:
      return 0U;
  }
}
}  // namespace

bool shouldReplacePendingTimeUpdate(const bool updatePending,
                                    const TimeSource pendingSource,
                                    const TimeSource incomingSource) {
  return !updatePending ||
         timeSourcePriority(incomingSource) >=
             timeSourcePriority(pendingSource);
}

bool shouldDiscardQueuedTimeUpdate(const bool updatePending,
                                   const TimeSource queuedSource,
                                   const TimeSource appliedSource) {
  return updatePending &&
         timeSourcePriority(queuedSource) <=
             timeSourcePriority(appliedSource);
}

bool monotonicIntervalElapsed(const uint32_t now, const uint32_t started,
                              const uint32_t duration) {
  return now - started >= duration;
}

int64_t extrapolateMonotonicEpoch(const int64_t baselineEpoch,
                                  const uint64_t baselineMicroseconds,
                                  const uint64_t nowMicroseconds) {
  if (!isValidEpoch(baselineEpoch) ||
      nowMicroseconds < baselineMicroseconds) {
    return 0;
  }
  const uint64_t elapsedSeconds =
      (nowMicroseconds - baselineMicroseconds) / 1000000ULL;
  if (elapsedSeconds >
      static_cast<uint64_t>(kMaximumValidEpoch - baselineEpoch)) {
    return 0;
  }
  return baselineEpoch + static_cast<int64_t>(elapsedSeconds);
}

SyncRoute syncRouteForSource(const TimeSource source) {
  switch (source) {
    case TimeSource::kBle:
      return SyncRoute::kBle;
    case TimeSource::kPortal:
      return SyncRoute::kPortal;
    case TimeSource::kNtp:
      return SyncRoute::kNtp;
    case TimeSource::kRtc:
    default:
      return SyncRoute::kUnselected;
  }
}

uint32_t remainingResyncDelayMs(const SyncRoute route,
                                const int64_t lastSyncEpoch,
                                const int64_t currentEpoch,
                                const uint32_t bleIntervalMs,
                                const uint32_t wifiIntervalMs) {
  uint32_t intervalMs = 0;
  if (route == SyncRoute::kBle) {
    intervalMs = bleIntervalMs;
  } else if (route == SyncRoute::kPortal ||
             route == SyncRoute::kNtp) {
    intervalMs = wifiIntervalMs;
  }
  if (intervalMs == 0 || !isValidEpoch(lastSyncEpoch) ||
      !isValidEpoch(currentEpoch) || currentEpoch < lastSyncEpoch) {
    return 0;
  }
  const int64_t elapsedSeconds = currentEpoch - lastSyncEpoch;
  const uint64_t elapsedMs =
      static_cast<uint64_t>(elapsedSeconds) * 1000ULL;
  return elapsedMs >= intervalMs
             ? 0
             : intervalMs - static_cast<uint32_t>(elapsedMs);
}

InitialSyncPhase initialSyncPhase(const uint32_t elapsedMs,
                                  const uint32_t bleOnlyWindowMs,
                                  const uint32_t setupWindowMs) {
  if (elapsedMs < bleOnlyWindowMs) {
    return InitialSyncPhase::kBleOnly;
  }
  if (elapsedMs < setupWindowMs) {
    return InitialSyncPhase::kPortal;
  }
  return InitialSyncPhase::kNtp;
}

bool parseTimeSyncPayload(const uint8_t* data, const size_t length,
                          int64_t& epoch, int16_t& utcOffsetMinutes) {
  if (data == nullptr || length == 0) {
    return false;
  }

  if (length == 12 && data[0] == 1) {
    uint64_t rawEpoch = 0;
    for (uint8_t i = 0; i < 8; ++i) {
      rawEpoch |= static_cast<uint64_t>(data[i + 1]) << (8U * i);
    }
    const uint16_t rawOffset =
        static_cast<uint16_t>(data[9]) |
        (static_cast<uint16_t>(data[10]) << 8U);
    epoch = static_cast<int64_t>(rawEpoch);
    utcOffsetMinutes = static_cast<int16_t>(rawOffset);
    return isValidEpoch(epoch) && isValidUtcOffset(utcOffsetMinutes);
  }

  if (length >= 48) {
    return false;
  }
  char text[48] = {};
  for (size_t i = 0; i < length; ++i) {
    if (data[i] == 0 || (!isprint(data[i]) && !isspace(data[i]))) {
      return false;
    }
    text[i] = static_cast<char>(data[i]);
  }

  char* separator = nullptr;
  const long long parsedEpoch = strtoll(text, &separator, 10);
  if (separator == text || (*separator != ',' && *separator != ' ')) {
    return false;
  }
  while (*separator == ',' || isspace(static_cast<unsigned char>(*separator))) {
    ++separator;
  }
  char* end = nullptr;
  const long parsedOffset = strtol(separator, &end, 10);
  while (end != nullptr && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (end == separator || (end != nullptr && *end != '\0')) {
    return false;
  }

  epoch = parsedEpoch;
  utcOffsetMinutes = static_cast<int16_t>(parsedOffset);
  return parsedOffset >= INT16_MIN && parsedOffset <= INT16_MAX &&
         isValidEpoch(epoch) && isValidUtcOffset(utcOffsetMinutes);
}

bool parsePortalTimeForm(const uint8_t* data, const size_t length,
                         int64_t& epoch, int16_t& utcOffsetMinutes) {
  if (data == nullptr || length == 0 ||
      length > kMaximumPortalTimeFormLength ||
      memchr(data, '\0', length) != nullptr) {
    return false;
  }

  char epochText[21] = {};
  char offsetText[7] = {};
  bool epochSeen = false;
  bool offsetSeen = false;
  size_t cursor = 0;
  while (cursor < length) {
    const size_t fieldBegin = cursor;
    while (cursor < length && data[cursor] != '&') {
      ++cursor;
    }
    const size_t fieldEnd = cursor;
    if (cursor < length) {
      ++cursor;
    }
    size_t equals = fieldBegin;
    while (equals < fieldEnd && data[equals] != '=') {
      ++equals;
    }
    if (equals == fieldBegin || equals == fieldEnd) {
      return false;
    }
    const size_t keyLength = equals - fieldBegin;
    const size_t valueLength = fieldEnd - equals - 1U;
    char* destination = nullptr;
    size_t destinationSize = 0;
    if (keyLength == 5 &&
        memcmp(data + fieldBegin, "epoch", keyLength) == 0 && !epochSeen) {
      epochSeen = true;
      destination = epochText;
      destinationSize = sizeof(epochText);
    } else if (keyLength == 6 &&
               memcmp(data + fieldBegin, "offset", keyLength) == 0 &&
               !offsetSeen) {
      offsetSeen = true;
      destination = offsetText;
      destinationSize = sizeof(offsetText);
    } else {
      return false;
    }
    if (valueLength == 0 || valueLength >= destinationSize) {
      return false;
    }
    memcpy(destination, data + equals + 1U, valueLength);
    destination[valueLength] = '\0';
  }
  if (!epochSeen || !offsetSeen) {
    return false;
  }
  char payload[sizeof(epochText) + sizeof(offsetText)] = {};
  const int written = snprintf(payload, sizeof(payload), "%s,%s",
                               epochText, offsetText);
  return written > 0 && static_cast<size_t>(written) < sizeof(payload) &&
         parseTimeSyncPayload(reinterpret_cast<const uint8_t*>(payload),
                              static_cast<size_t>(written), epoch,
                              utcOffsetMinutes);
}

UserDisplayState selectUserDisplayState(
    const bool recoveryButtonHeld, const bool portalActive,
    const bool wifiBusy, const bool hasValidTime,
    const bool pairingAvailable, const bool syncOverdue) {
  if (recoveryButtonHeld) {
    return UserDisplayState::kRecovery;
  }
  if (hasValidTime && syncOverdue) {
    return UserDisplayState::kClock;
  }
  if (pairingAvailable) {
    return UserDisplayState::kPairing;
  }
  if (portalActive) {
    return UserDisplayState::kPortal;
  }
  if (wifiBusy) {
    return UserDisplayState::kWifi;
  }
  return hasValidTime ? UserDisplayState::kClock
                      : UserDisplayState::kNoTime;
}

DisplayFrame makeDisplayFrame(const uint32_t nowMs,
                              const bool hasValidTime,
                              const bool timezoneFresh,
                              const UserDisplayState state,
                              const uint32_t pairingDisplayMs) {
  bool colonOn = false;
  if (hasValidTime && !timezoneFresh) {
    colonOn = (nowMs % 2000U) < 250U;
  } else {
    // A conventional one-Hertz clock cadence: 500 ms on, 500 ms off.
    colonOn = (nowMs % 1000U) < 500U;
  }

  switch (state) {
    case UserDisplayState::kPairing:
      return {DisplayContent::kPairing, colonOn};
    case UserDisplayState::kPortal:
      return {hasValidTime && (nowMs % 4000U) >= pairingDisplayMs
                  ? DisplayContent::kTime
                  : DisplayContent::kPortal,
              colonOn};
    case UserDisplayState::kWifi:
      return {hasValidTime && (nowMs % 6000U) >= 1000U
                  ? DisplayContent::kTime
                  : DisplayContent::kWifi,
              colonOn};
    case UserDisplayState::kNoTime:
      return {DisplayContent::kNoTime, colonOn};
    case UserDisplayState::kRecovery:
      return {DisplayContent::kRecovery, colonOn};
    case UserDisplayState::kClock:
    default:
      return {DisplayContent::kTime, colonOn};
  }
}

LightLevelController::LightLevelController()
    : filteredLux_(0.0F), initialized_(false), level_(0) {}

void LightLevelController::reset() {
  filteredLux_ = 0.0F;
  initialized_ = false;
  level_ = 0;
}

uint8_t LightLevelController::update(float lux) {
  if (!(lux >= 0.0F) || lux > 100000.0F) {
    return level_;
  }
  if (!initialized_) {
    filteredLux_ = lux;
    initialized_ = true;
  } else {
    // About a six-second settling time at one sample per second.
    filteredLux_ += 0.18F * (lux - filteredLux_);
  }

  static constexpr float kBoundaries[7] = {
      1.2F, 3.0F, 12.0F, 45.0F, 160.0F, 500.0F, 1400.0F};

  // A 20% hysteresis band prevents visible flicker near a boundary.
  while (level_ < 7 && filteredLux_ > kBoundaries[level_] * 1.20F) {
    ++level_;
  }
  while (level_ > 0 && filteredLux_ < kBoundaries[level_ - 1] * 0.80F) {
    --level_;
  }
  return level_;
}

bool BssidAttemptTracker::contains(const uint8_t bssid[6]) const {
  if (bssid == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < size_; ++i) {
    if (memcmp(entries_[i], bssid, 6) == 0) {
      return true;
    }
  }
  return false;
}

bool BssidAttemptTracker::add(const uint8_t bssid[6]) {
  if (bssid == nullptr || contains(bssid) || size_ >= kCapacity) {
    return false;
  }
  memcpy(entries_[size_++], bssid, 6);
  return true;
}

bool WifiCandidateRanker::consider(const char* ssid,
                                   const uint8_t bssid[6],
                                   const int32_t channel,
                                   const int32_t rssi) {
  if (ssid == nullptr || bssid == nullptr || ssid[0] == '\0' ||
      channel < 1 || channel > 14) {
    return false;
  }
  size_t ssidLength = 0;
  while (ssidLength <= WifiCandidate::kMaximumSsidLength &&
         ssid[ssidLength] != '\0') {
    ++ssidLength;
  }
  if (ssidLength == 0 ||
      ssidLength > WifiCandidate::kMaximumSsidLength) {
    return false;
  }

  for (uint8_t i = 0; i < size_; ++i) {
    if (memcmp(candidates_[i].bssid, bssid, 6) == 0) {
      if (rssi <= candidates_[i].rssi) {
        return false;
      }
      for (uint8_t j = i; j + 1U < size_; ++j) {
        candidates_[j] = candidates_[j + 1U];
      }
      --size_;
      break;
    }
  }

  uint8_t insertionIndex = 0;
  while (insertionIndex < size_ &&
         (candidates_[insertionIndex].rssi > rssi ||
          (candidates_[insertionIndex].rssi == rssi &&
           memcmp(candidates_[insertionIndex].bssid, bssid, 6) <= 0))) {
    ++insertionIndex;
  }
  if (insertionIndex >= kCapacity) {
    return false;
  }

  const uint8_t newSize = size_ < kCapacity ? size_ + 1U : size_;
  for (uint8_t i = newSize - 1U; i > insertionIndex; --i) {
    candidates_[i] = candidates_[i - 1U];
  }
  WifiCandidate candidate = {};
  memcpy(candidate.ssid, ssid, ssidLength);
  candidate.ssid[ssidLength] = '\0';
  memcpy(candidate.bssid, bssid, sizeof(candidate.bssid));
  candidate.channel = channel;
  candidate.rssi = rssi;
  candidates_[insertionIndex] = candidate;
  size_ = newSize;
  return true;
}

const WifiCandidate* WifiCandidateRanker::at(const uint8_t index) const {
  return index < size_ ? &candidates_[index] : nullptr;
}

}  // namespace clockcore
