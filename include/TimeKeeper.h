#pragma once

#include <RTClib.h>
#include <Preferences.h>

#include "ClockTypes.h"

class TimeKeeper {
 public:
  bool begin();
  bool apply(const TimeUpdate& update);
  bool hasValidTime() const;
  bool hasConfirmedSync() const { return hasConfirmedSync_; }
  int64_t utcNow() const;
  int16_t utcOffsetMinutes() const { return utcOffsetMinutes_; }
  bool timezoneFresh() const { return timezoneFresh_; }
  SyncRoute syncRoute() const { return syncRoute_; }
  int64_t lastSyncUtc() const { return lastSyncUtc_; }
  void localTime(struct tm& result) const;
  bool rtcAvailable() const { return rtcAvailable_; }
  void clearSyncTrust();

 private:
  RTC_DS3231 rtc_;
  Preferences preferences_;
  int64_t lastSyncUtc_ = 0;
  int64_t correctionBaselineUtc_ = 0;
  uint64_t correctionBaselineUs_ = 0;
  int16_t utcOffsetMinutes_ = 0;
  SyncRoute syncRoute_ = SyncRoute::kUnselected;
  bool rtcAvailable_ = false;
  bool timezoneFresh_ = false;
  bool hasConfirmedSync_ = false;
  bool correctionBaselineValid_ = false;
};
