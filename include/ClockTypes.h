#pragma once

#include <stdint.h>

enum class TimeSource : uint8_t {
  kRtc,
  kBle,
  kPortal,
  kNtp,
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
