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
  const uint8_t alternateAddress =
      config::kBh1750Address == 0x23 ? 0x5C : 0x23;
  const uint8_t addresses[2] = {config::kBh1750Address, alternateAddress};
  lightSensorAvailable_ = false;
  for (const uint8_t address : addresses) {
    if (lightSensor_.begin(BH1750::ONE_TIME_HIGH_RES_MODE, address)) {
      lightSensorAvailable_ = true;
      lightSensorAddress_ = address;
      Serial.printf("BH1750 detected at I2C address 0x%02X\n", address);
#if CLOCK_LIGHT_DIAGNOSTICS
      Serial.printf("LIGHT sensor=ready address=0x%02X sample_ms=%lu\n",
                    address,
                    static_cast<unsigned long>(config::kLightSampleMs));
#endif
      break;
    }
  }
  invalidLightReadings_ = 0;
  unreadyLightReadings_ = 0;
  if (!lightSensorAvailable_ && displayAvailable_) {
    display_.setBrightness(config::kFallbackBrightness);
  }
#if CLOCK_LIGHT_DIAGNOSTICS
  if (!lightSensorAvailable_) {
    Serial.printf("LIGHT sensor=missing fallback_level=%u\n",
                  config::kFallbackBrightness);
  }
#endif
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
  if (!lightSensorAvailable_) {
    return;
  }
  if (!lightSensor_.measurementReady(false)) {
#if CLOCK_LIGHT_DIAGNOSTICS
    Serial.printf("LIGHT sample=not-ready consecutive=%u\n",
                  static_cast<unsigned>(unreadyLightReadings_ + 1U));
#endif
    if (++unreadyLightReadings_ >= 5) {
      lightSensorAvailable_ = false;
      lightLevel_.reset();
      if (displayAvailable_) {
        display_.setBrightness(config::kFallbackBrightness);
      }
      Serial.printf("BH1750 at 0x%02X stopped producing measurements\n",
                    lightSensorAddress_);
    }
    return;
  }
  unreadyLightReadings_ = 0;
  const float lux = lightSensor_.readLightLevel();
  const bool nextMeasurementArmed =
      lightSensor_.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
  if (!nextMeasurementArmed) {
    lightSensorAvailable_ = false;
    lightLevel_.reset();
    if (displayAvailable_) {
      display_.setBrightness(config::kFallbackBrightness);
    }
    Serial.printf("BH1750 at 0x%02X could not start next measurement\n",
                  lightSensorAddress_);
    return;
  }
  if (!(lux >= 0.0F) || lux > 100000.0F) {
#if CLOCK_LIGHT_DIAGNOSTICS
    Serial.printf("LIGHT sample=invalid raw_lux=%.2f consecutive=%u\n",
                  static_cast<double>(lux),
                  static_cast<unsigned>(invalidLightReadings_ + 1U));
#endif
    if (++invalidLightReadings_ >= 5) {
      lightSensorAvailable_ = false;
      lightLevel_.reset();
      if (displayAvailable_) {
        display_.setBrightness(config::kFallbackBrightness);
      }
      Serial.printf("BH1750 at 0x%02X returned invalid measurements\n",
                    lightSensorAddress_);
    }
  } else {
    invalidLightReadings_ = 0;
    const uint8_t brightnessLevel = lightLevel_.update(lux);
#if CLOCK_LIGHT_DIAGNOSTICS
    Serial.printf("LIGHT raw_lux=%.2f filtered_lux=%.2f level=%u\n",
                  static_cast<double>(lux),
                  static_cast<double>(lightLevel_.filteredLux()),
                  static_cast<unsigned>(brightnessLevel));
#endif
    if (displayAvailable_) {
      display_.setBrightness(brightnessLevel);
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
                             const UserDisplayState state) {
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
      now, clock.hasValidTime(), clock.timezoneFresh(), state,
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
