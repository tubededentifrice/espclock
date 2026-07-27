#include "ClockCore.h"

#include <ctype.h>
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
         candidates_[insertionIndex].rssi >= rssi) {
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
