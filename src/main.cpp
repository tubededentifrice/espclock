#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>

#include "AppConfig.h"
#include "BleTimeService.h"
#include "ClockCore.h"
#include "Diagnostics.h"
#include "DisplayController.h"
#include "NetworkTimeService.h"
#include "TimeKeeper.h"

namespace {
TimeKeeper clockTime;
DisplayController clockDisplay;
BleTimeService bleTime;
NetworkTimeService networkTime;
portMUX_TYPE timeUpdateMux = portMUX_INITIALIZER_UNLOCKED;
TimeUpdate pendingTimeUpdate = {};
bool timeUpdatePending = false;
bool recoveryButtonHeld = false;
bool recoveryResetArmed = false;
uint32_t recoveryButtonPressedMs = 0;

uint8_t sourcePriority(const TimeSource source) {
  switch (source) {
    case TimeSource::kBle:
      return 3;
    case TimeSource::kPortal:
      return 2;
    case TimeSource::kNtp:
      return 1;
    case TimeSource::kRtc:
    default:
      return 0;
  }
}

void enqueueTimeUpdate(const TimeUpdate& update) {
  portENTER_CRITICAL(&timeUpdateMux);
  if (!timeUpdatePending ||
      sourcePriority(update.source) >=
          sourcePriority(pendingTimeUpdate.source)) {
    pendingTimeUpdate = update;
    timeUpdatePending = true;
  }
  portEXIT_CRITICAL(&timeUpdateMux);
}

bool dequeueTimeUpdate(TimeUpdate& update) {
  bool available = false;
  portENTER_CRITICAL(&timeUpdateMux);
  if (timeUpdatePending) {
    update = pendingTimeUpdate;
    timeUpdatePending = false;
    available = true;
  }
  portEXIT_CRITICAL(&timeUpdateMux);
  return available;
}

void handleRecoveryButton() {
  const uint32_t now = millis();
  const bool pressed = digitalRead(config::kRecoveryButtonPin) == LOW;
  if (pressed) {
    if (!recoveryButtonHeld) {
      recoveryButtonHeld = true;
      recoveryButtonPressedMs = now;
    } else if (!recoveryResetArmed &&
               now - recoveryButtonPressedMs >= config::kRecoveryHoldMs) {
      recoveryResetArmed = true;
      CLOCK_DIAGNOSTIC_PRINTLN(
          "Recovery armed; release BOOT to clear sync trust and BLE bonds");
    }
    return;
  }

  if (recoveryResetArmed) {
    clockTime.clearSyncTrust();
    bleTime.clearBonds();
    CLOCK_DIAGNOSTIC_PRINTLN("Recovery complete; restarting");
    delay(100);
    ESP.restart();
  }
  recoveryButtonHeld = false;
  recoveryResetArmed = false;
}

UserDisplayState displayState() {
  return clockcore::selectUserDisplayState(
      recoveryButtonHeld, networkTime.portalActive(), networkTime.wifiBusy(),
      clockTime.hasValidTime(),
      bleTime.advertising() && bleTime.pairingOpen(),
      networkTime.backgroundRefreshActive());
}
}  // namespace

void setup() {
#if CLOCK_ENABLE_DIAGNOSTICS
  CLOCK_DIAGNOSTIC_BEGIN(115200);
  delay(100);
#endif
#if CLOCK_CPU_FREQUENCY_MHZ > 0
  if (!setCpuFrequencyMhz(config::kCpuFrequencyMhz)) {
    CLOCK_DIAGNOSTIC_PRINTF("CPU frequency change to %u MHz failed\n",
                            config::kCpuFrequencyMhz);
  }
#endif
  pinMode(config::kRecoveryButtonPin, INPUT_PULLUP);
  Wire.begin(config::kI2cSdaPin, config::kI2cSclPin);

  clockTime.begin();
  clockDisplay.begin();
  bleTime.begin(enqueueTimeUpdate, clockTime.hasConfirmedSync());
  networkTime.begin(enqueueTimeUpdate, clockTime.utcOffsetMinutes(),
                    clockTime.hasConfirmedSync());

  CLOCK_DIAGNOSTIC_PRINTF(
      "Kids Clock boot: display=%s/%s, light=%s, RTC=%s, time=%s, "
      "offset=%d min, cpu=%u MHz\n",
                clockDisplay.displayName(),
                clockDisplay.displayAvailable() ? "ready" : "missing",
                clockDisplay.lightSensorAvailable() ? "ready" : "missing",
                clockTime.rtcAvailable() ? "present" : "missing",
                clockTime.hasValidTime() ? "valid" : "needed",
                clockTime.utcOffsetMinutes(), getCpuFrequencyMhz());
#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_SSD1306
  CLOCK_DIAGNOSTIC_PRINTF(
      "OLED configured at I2C address 0x%02X (%dx%d)\n",
      config::kOledAddress, CLOCK_OLED_WIDTH, CLOCK_OLED_HEIGHT);
#endif
}

void loop() {
  handleRecoveryButton();

  TimeUpdate update = {};
  if (dequeueTimeUpdate(update)) {
    const bool applied = clockTime.apply(update);
    if (update.source == TimeSource::kBle) {
      bleTime.reportTimeResult(applied);
    }
    if (applied) {
      bleTime.markTimeConfirmed();
      networkTime.onExternalTimeSync(update.utcOffsetMinutes);
      CLOCK_DIAGNOSTIC_PRINTF(
          "Time synchronized: source=%u epoch=%lld offset=%d\n",
          static_cast<unsigned>(update.source),
          static_cast<long long>(update.unixUtc),
          update.utcOffsetMinutes);
    }
  }

  bleTime.tick(clockTime);
  networkTime.tick(bleTime.connected());
  clockDisplay.tick(clockTime, displayState());
  delay(config::kMainLoopDelayMs);
}
