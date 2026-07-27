#pragma once

#include <stdint.h>

enum class TimeSource : uint8_t {
  kRtc,
  kBle,
  kPortal,
  kNtp,
};

// Values intentionally match the external TimeSource values stored in NVS.
enum class SyncRoute : uint8_t {
  kUnselected = 0,
  kBle = 1,
  kPortal = 2,
  kNtp = 3,
};

struct TimeUpdate {
  int64_t unixUtc;
  int16_t utcOffsetMinutes;
  TimeSource source;
};

using TimeUpdateHandler = void (*)(const TimeUpdate&);

enum class UserDisplayState : uint8_t {
  kClock,
  kPairing,
  kPortal,
  kWifi,
  kNoTime,
  kRecovery,
};
