#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ClockTypes.h"

namespace clockcore {

constexpr int64_t kMinimumValidEpoch = 1704067200LL;  // 2024-01-01 UTC
constexpr int64_t kMaximumValidEpoch = 4102444799LL;  // 2099-12-31 UTC

bool isValidEpoch(int64_t epoch);
bool isValidUtcOffset(int offsetMinutes);
bool isPlausibleCorrection(int64_t currentEpoch, int64_t candidateEpoch);
bool isAcceptableCorrection(bool hasConfirmedSync, int64_t currentEpoch,
                            int64_t candidateEpoch);

// Accepts either "epoch,offset" UTF-8 text or a 12-byte little-endian packet:
// version(1), Unix UTC seconds(8), signed UTC offset minutes(2), flags(1).
bool parseTimeSyncPayload(const uint8_t* data, size_t length,
                          int64_t& epoch, int16_t& utcOffsetMinutes);

enum class DisplayContent : uint8_t {
  kTime,
  kPairing,
  kPortal,
  kWifi,
  kNoTime,
  kRecovery,
};

struct DisplayFrame {
  DisplayContent content;
  bool colonOn;
};

DisplayFrame makeDisplayFrame(uint32_t nowMs, bool hasValidTime,
                              bool timezoneFresh, UserDisplayState state,
                              uint32_t pairingDisplayMs);

class LightLevelController {
 public:
  LightLevelController();
  void reset();
  uint8_t update(float lux);
  uint8_t level() const { return level_; }
  float filteredLux() const { return filteredLux_; }

 private:
  float filteredLux_;
  bool initialized_;
  uint8_t level_;
};

class BssidAttemptTracker {
 public:
  static constexpr uint8_t kCapacity = 24;

  bool contains(const uint8_t bssid[6]) const;
  bool add(const uint8_t bssid[6]);
  uint8_t size() const { return size_; }
  bool full() const { return size_ >= kCapacity; }

 private:
  uint8_t entries_[kCapacity][6] = {};
  uint8_t size_ = 0;
};

}  // namespace clockcore
