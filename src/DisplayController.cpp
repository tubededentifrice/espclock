#include "DisplayController.h"

#include <Arduino.h>

#include "AppConfig.h"
#include "TimeKeeper.h"

DisplayController::DisplayController()
    : display_(selectedDisplayBackend()) {}

bool DisplayController::begin() {
  displayAvailable_ = display_.begin();
  if (displayAvailable_) {
    display_.setBrightness(config::kFallbackBrightness);
  }
  tryLightSensor();
  return displayAvailable_;
}

void DisplayController::tryLightSensor() {
  lastLightSensorAttemptMs_ = millis();
  lightLevel_.reset();
  lightSensorAvailable_ =
      lightSensor_.begin(BH1750::CONTINUOUS_HIGH_RES_MODE,
                         config::kBh1750Address);
  invalidLightReadings_ = 0;
  if (!lightSensorAvailable_ && displayAvailable_) {
    display_.setBrightness(config::kFallbackBrightness);
  }
}

void DisplayController::sampleLight() {
  const uint32_t now = millis();
  if (!lightSensorAvailable_ &&
      now - lastLightSensorAttemptMs_ >= config::kLightSensorRetryMs) {
    tryLightSensor();
  }
  if (now - lastLightSampleMs_ < config::kLightSampleMs) {
    return;
  }
  lastLightSampleMs_ = now;
  if (lightSensorAvailable_ && lightSensor_.measurementReady(true)) {
    const float lux = lightSensor_.readLightLevel();
    if (!(lux >= 0.0F) || lux > 100000.0F) {
      if (++invalidLightReadings_ >= 5) {
        lightSensorAvailable_ = false;
        lightLevel_.reset();
        if (displayAvailable_) {
          display_.setBrightness(config::kFallbackBrightness);
        }
      }
    } else {
      invalidLightReadings_ = 0;
      if (displayAvailable_) {
        display_.setBrightness(lightLevel_.update(lux));
      }
    }
  }
}

void DisplayController::showClock(const TimeKeeper& clock) {
  struct tm local = {};
  clock.localTime(local);
  display_.showTime(static_cast<uint8_t>(local.tm_hour),
                    static_cast<uint8_t>(local.tm_min), colonOn_);
}

void DisplayController::showMessage(const DisplayMessage message) {
  display_.showMessage(message);
}

void DisplayController::tick(const TimeKeeper& clock,
                             const UserDisplayState state,
                             const bool bleConnected) {
  sampleLight();
  if (!displayAvailable_) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastDisplayMs_ < 250) {
    return;
  }
  lastDisplayMs_ = now;
  const clockcore::DisplayFrame frame = clockcore::makeDisplayFrame(
      now, clock.hasValidTime(), clock.timezoneFresh(), bleConnected, state,
      config::kPairingDisplayMs);
  colonOn_ = frame.colonOn;
  switch (frame.content) {
    case clockcore::DisplayContent::kTime:
      showClock(clock);
      break;
    case clockcore::DisplayContent::kPairing:
      showMessage(DisplayMessage::kPairing);
      break;
    case clockcore::DisplayContent::kPortal:
      showMessage(DisplayMessage::kPortal);
      break;
    case clockcore::DisplayContent::kWifi:
      showMessage(DisplayMessage::kWifi);
      break;
    case clockcore::DisplayContent::kRecovery:
      showMessage(DisplayMessage::kRecovery);
      break;
    case clockcore::DisplayContent::kNoTime:
    default:
      showMessage(DisplayMessage::kNoTime);
      break;
  }
}
