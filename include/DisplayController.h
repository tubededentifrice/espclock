#pragma once

#include <BH1750.h>

#include "ClockCore.h"
#include "ClockTypes.h"
#include "DisplayBackend.h"

class TimeKeeper;

class DisplayController {
 public:
  DisplayController();
  bool begin();
  void tick(const TimeKeeper& clock, UserDisplayState state, bool bleConnected);
  bool displayAvailable() const { return displayAvailable_; }
  bool lightSensorAvailable() const { return lightSensorAvailable_; }
  const char* displayName() const { return display_.name(); }

 private:
  void sampleLight();
  void showClock(const TimeKeeper& clock);
  void showMessage(DisplayMessage message);
  void tryLightSensor();

  DisplayBackend& display_;
  BH1750 lightSensor_;
  clockcore::LightLevelController lightLevel_;
  uint32_t lastLightSampleMs_ = 0;
  uint32_t lastLightSensorAttemptMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  bool displayAvailable_ = false;
  bool lightSensorAvailable_ = false;
  uint8_t invalidLightReadings_ = 0;
  bool colonOn_ = false;
};
