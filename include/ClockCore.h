#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ClockTypes.h"

namespace clockcore {

constexpr int64_t kMinimumValidEpoch = 1704067200LL;  // 2024-01-01 UTC
constexpr int64_t kMaximumValidEpoch = 4102444799LL;  // 2099-12-31 UTC
constexpr size_t kMaximumPortalTimeFormLength = 64;
constexpr size_t kPersistedSyncRecordSize = 16;

bool isValidEpoch(int64_t epoch);
bool isValidUtcOffset(int offsetMinutes);
bool isPlausibleCorrection(int64_t currentEpoch, int64_t candidateEpoch);
bool isAcceptableCorrection(bool hasConfirmedSync, int64_t currentEpoch,
                            int64_t candidateEpoch);
bool isValidSyncRouteValue(uint8_t value);
bool isValidPersistedSyncState(bool confirmed, int64_t lastSyncEpoch,
                               uint8_t routeValue, int utcOffsetMinutes);
bool encodePersistedSyncState(int64_t lastSyncEpoch, SyncRoute route,
                              int16_t utcOffsetMinutes, uint8_t* output,
                              size_t outputSize);
bool decodePersistedSyncState(const uint8_t* data, size_t length,
                              int64_t& lastSyncEpoch, SyncRoute& route,
                              int16_t& utcOffsetMinutes);
bool shouldReplacePendingTimeUpdate(bool updatePending,
                                    TimeSource pendingSource,
                                    TimeSource incomingSource);
bool shouldDiscardQueuedTimeUpdate(bool updatePending,
                                   TimeSource queuedSource,
                                   TimeSource appliedSource);
bool monotonicIntervalElapsed(uint32_t now, uint32_t started,
                              uint32_t duration);
int64_t extrapolateMonotonicEpoch(int64_t baselineEpoch,
                                  uint64_t baselineMicroseconds,
                                  uint64_t nowMicroseconds);
SyncRoute syncRouteForSource(TimeSource source);
uint32_t remainingResyncDelayMs(SyncRoute route, int64_t lastSyncEpoch,
                                int64_t currentEpoch,
                                uint32_t bleIntervalMs,
                                uint32_t wifiIntervalMs);

enum class InitialSyncPhase : uint8_t {
  kBleOnly,
  kPortal,
  kNtp,
};

InitialSyncPhase initialSyncPhase(uint32_t elapsedMs,
                                  uint32_t bleOnlyWindowMs,
                                  uint32_t setupWindowMs);

// Accepts either "epoch,offset" UTF-8 text or a 12-byte little-endian packet:
// version(1), Unix UTC seconds(8), signed UTC offset minutes(2), flags(1).
bool parseTimeSyncPayload(const uint8_t* data, size_t length,
                          int64_t& epoch, int16_t& utcOffsetMinutes);
bool parsePortalTimeForm(const uint8_t* data, size_t length,
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

UserDisplayState selectUserDisplayState(bool recoveryButtonHeld,
                                        bool portalActive, bool wifiBusy,
                                        bool hasValidTime,
                                        bool pairingAvailable,
                                        bool syncOverdue);

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

struct WifiCandidate {
  static constexpr uint8_t kMaximumSsidLength = 32;

  char ssid[kMaximumSsidLength + 1] = {};
  uint8_t bssid[6] = {};
  int32_t channel = 0;
  int32_t rssi = -127;
};

class WifiCandidateRanker {
 public:
  static constexpr uint8_t kCapacity = 6;

  void clear() { size_ = 0; }
  bool consider(const char* ssid, const uint8_t bssid[6], int32_t channel,
                int32_t rssi);
  const WifiCandidate* at(uint8_t index) const;
  uint8_t size() const { return size_; }

 private:
  WifiCandidate candidates_[kCapacity] = {};
  uint8_t size_ = 0;
};

}  // namespace clockcore
