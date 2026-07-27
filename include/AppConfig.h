#pragma once

#include <stdint.h>

// Every setting below can be overridden with a PlatformIO -D build flag.
#define CLOCK_DISPLAY_TM1637 1
#define CLOCK_DISPLAY_SSD1306 2

#ifndef CLOCK_DISPLAY_DRIVER
#define CLOCK_DISPLAY_DRIVER CLOCK_DISPLAY_TM1637
#endif

#ifndef CLOCK_TM1637_CLK_PIN
#define CLOCK_TM1637_CLK_PIN 4
#endif

#ifndef CLOCK_TM1637_DIO_PIN
#define CLOCK_TM1637_DIO_PIN 3
#endif

#ifndef CLOCK_I2C_SDA_PIN
#define CLOCK_I2C_SDA_PIN 6
#endif

#ifndef CLOCK_I2C_SCL_PIN
#define CLOCK_I2C_SCL_PIN 7
#endif

#ifndef CLOCK_BH1750_ADDRESS
#define CLOCK_BH1750_ADDRESS 0x23
#endif

#ifndef CLOCK_OLED_WIDTH
#define CLOCK_OLED_WIDTH 128
#endif

#ifndef CLOCK_OLED_HEIGHT
#define CLOCK_OLED_HEIGHT 64
#endif

#ifndef CLOCK_OLED_ADDRESS
#define CLOCK_OLED_ADDRESS 0x3C
#endif

#ifndef CLOCK_FALLBACK_BRIGHTNESS
#define CLOCK_FALLBACK_BRIGHTNESS 2
#endif

#ifndef CLOCK_LIGHT_SENSOR_RETRY_MS
#define CLOCK_LIGHT_SENSOR_RETRY_MS 30000UL
#endif

#ifndef CLOCK_MAIN_LOOP_DELAY_MS
#define CLOCK_MAIN_LOOP_DELAY_MS 20UL
#endif

#ifndef CLOCK_CPU_FREQUENCY_MHZ
#define CLOCK_CPU_FREQUENCY_MHZ 0
#endif

#ifndef CLOCK_ENABLE_OPEN_WIFI_FALLBACK
#define CLOCK_ENABLE_OPEN_WIFI_FALLBACK 1
#endif

#ifndef CLOCK_DEFAULT_UTC_OFFSET_MINUTES
#define CLOCK_DEFAULT_UTC_OFFSET_MINUTES 0
#endif

#ifndef CLOCK_BLE_WINDOW_MS
#define CLOCK_BLE_WINDOW_MS 10000UL
#endif

#ifndef CLOCK_NEW_BLE_PAIRING_WINDOW_MS
#define CLOCK_NEW_BLE_PAIRING_WINDOW_MS 120000UL
#endif

#ifndef CLOCK_BLE_FAST_ADV_MIN_MS
#define CLOCK_BLE_FAST_ADV_MIN_MS 30UL
#endif

#ifndef CLOCK_BLE_FAST_ADV_MAX_MS
#define CLOCK_BLE_FAST_ADV_MAX_MS 60UL
#endif

#ifndef CLOCK_BLE_SLOW_ADV_MIN_MS
#define CLOCK_BLE_SLOW_ADV_MIN_MS 800UL
#endif

#ifndef CLOCK_BLE_SLOW_ADV_MAX_MS
#define CLOCK_BLE_SLOW_ADV_MAX_MS 1000UL
#endif

#ifndef CLOCK_BLE_RESYNC_GRACE_MS
#define CLOCK_BLE_RESYNC_GRACE_MS 90000UL
#endif

#ifndef CLOCK_INITIAL_SETUP_WINDOW_MS
#define CLOCK_INITIAL_SETUP_WINDOW_MS 120000UL
#endif

#ifndef CLOCK_WIFI_CONNECT_TIMEOUT_MS
#define CLOCK_WIFI_CONNECT_TIMEOUT_MS 12000UL
#endif

#ifndef CLOCK_NTP_TIMEOUT_MS
#define CLOCK_NTP_TIMEOUT_MS 18000UL
#endif

#ifndef CLOCK_RESYNC_INTERVAL_MS
#define CLOCK_RESYNC_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)
#endif

#ifndef CLOCK_WIFI_RESYNC_INTERVAL_MS
#define CLOCK_WIFI_RESYNC_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)
#endif

#ifndef CLOCK_WIFI_WINDOW_MS
#define CLOCK_WIFI_WINDOW_MS (10UL * 60UL * 1000UL)
#endif

#ifndef CLOCK_LIGHT_SAMPLE_MS
#define CLOCK_LIGHT_SAMPLE_MS 1000UL
#endif

#ifndef CLOCK_ENABLE_DIAGNOSTICS
#define CLOCK_ENABLE_DIAGNOSTICS 0
#endif

#ifndef CLOCK_LIGHT_DIAGNOSTICS
#define CLOCK_LIGHT_DIAGNOSTICS 0
#endif

#ifndef CLOCK_PAIRING_DISPLAY_MS
#define CLOCK_PAIRING_DISPLAY_MS 1500UL
#endif

#ifndef CLOCK_RECOVERY_BUTTON_PIN
#define CLOCK_RECOVERY_BUTTON_PIN 9
#endif

#ifndef CLOCK_RECOVERY_HOLD_MS
#define CLOCK_RECOVERY_HOLD_MS 5000UL
#endif

namespace config {
constexpr uint8_t kTm1637ClkPin = CLOCK_TM1637_CLK_PIN;
constexpr uint8_t kTm1637DioPin = CLOCK_TM1637_DIO_PIN;
constexpr uint8_t kI2cSdaPin = CLOCK_I2C_SDA_PIN;
constexpr uint8_t kI2cSclPin = CLOCK_I2C_SCL_PIN;
constexpr uint8_t kBh1750Address = CLOCK_BH1750_ADDRESS;
constexpr uint8_t kOledAddress = CLOCK_OLED_ADDRESS;
constexpr uint8_t kFallbackBrightness = CLOCK_FALLBACK_BRIGHTNESS;
constexpr uint32_t kLightSensorRetryMs = CLOCK_LIGHT_SENSOR_RETRY_MS;
constexpr uint32_t kMainLoopDelayMs = CLOCK_MAIN_LOOP_DELAY_MS;
constexpr uint16_t kCpuFrequencyMhz = CLOCK_CPU_FREQUENCY_MHZ;
constexpr int16_t kDefaultUtcOffsetMinutes = CLOCK_DEFAULT_UTC_OFFSET_MINUTES;
constexpr uint32_t kBleWindowMs = CLOCK_BLE_WINDOW_MS;
constexpr uint32_t kNewBlePairingWindowMs =
    CLOCK_NEW_BLE_PAIRING_WINDOW_MS;
constexpr uint32_t kBleFastAdvertisingMinMs =
    CLOCK_BLE_FAST_ADV_MIN_MS;
constexpr uint32_t kBleFastAdvertisingMaxMs =
    CLOCK_BLE_FAST_ADV_MAX_MS;
constexpr uint32_t kBleSlowAdvertisingMinMs =
    CLOCK_BLE_SLOW_ADV_MIN_MS;
constexpr uint32_t kBleSlowAdvertisingMaxMs =
    CLOCK_BLE_SLOW_ADV_MAX_MS;
constexpr uint32_t kBleResyncGraceMs = CLOCK_BLE_RESYNC_GRACE_MS;
constexpr uint32_t kInitialSetupWindowMs =
    CLOCK_INITIAL_SETUP_WINDOW_MS;
constexpr uint32_t kWifiConnectTimeoutMs = CLOCK_WIFI_CONNECT_TIMEOUT_MS;
constexpr uint32_t kNtpTimeoutMs = CLOCK_NTP_TIMEOUT_MS;
constexpr uint32_t kResyncIntervalMs = CLOCK_RESYNC_INTERVAL_MS;
constexpr uint32_t kWifiResyncIntervalMs =
    CLOCK_WIFI_RESYNC_INTERVAL_MS;
constexpr uint32_t kLightSampleMs = CLOCK_LIGHT_SAMPLE_MS;
constexpr uint32_t kPairingDisplayMs = CLOCK_PAIRING_DISPLAY_MS;
constexpr uint8_t kRecoveryButtonPin = CLOCK_RECOVERY_BUTTON_PIN;
constexpr uint32_t kRecoveryHoldMs = CLOCK_RECOVERY_HOLD_MS;
constexpr uint16_t kEarliestValidYear = 2024;
constexpr uint16_t kLatestValidYear = 2099;
constexpr uint8_t kMaximumFailedNetworks = 24;
constexpr uint8_t kMaximumWifiAttemptsPerWindow = 6;
constexpr uint32_t kWifiWindowMs = CLOCK_WIFI_WINDOW_MS;

static_assert(CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_TM1637 ||
                  CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_SSD1306,
              "Unsupported CLOCK_DISPLAY_DRIVER");
static_assert(CLOCK_INITIAL_SETUP_WINDOW_MS > CLOCK_BLE_WINDOW_MS,
              "Initial setup window must outlast the BLE-only grace");
static_assert(CLOCK_INITIAL_SETUP_WINDOW_MS < 0x80000000UL &&
                  CLOCK_BLE_RESYNC_GRACE_MS < 0x80000000UL &&
                  CLOCK_RESYNC_INTERVAL_MS < 0x80000000UL &&
                  CLOCK_WIFI_RESYNC_INTERVAL_MS < 0x80000000UL &&
                  CLOCK_WIFI_WINDOW_MS < 0x80000000UL,
              "Monotonic deadlines must remain below half of millis() range");

#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_SSD1306
static_assert(CLOCK_OLED_WIDTH == 128,
              "The SSD1306 backend currently supports 128-pixel displays");
static_assert(CLOCK_OLED_HEIGHT == 32 || CLOCK_OLED_HEIGHT == 64,
              "CLOCK_OLED_HEIGHT must be 32 or 64");
static_assert(CLOCK_OLED_ADDRESS > 0 && CLOCK_OLED_ADDRESS <= 0x7F,
              "CLOCK_OLED_ADDRESS must be a valid 7-bit I2C address");
#endif

static_assert(kFallbackBrightness <= 7,
              "CLOCK_FALLBACK_BRIGHTNESS must be between 0 and 7");
static_assert(kMainLoopDelayMs >= 1 && kMainLoopDelayMs <= 100,
              "CLOCK_MAIN_LOOP_DELAY_MS must be between 1 and 100");
static_assert(kBh1750Address == 0x23 || kBh1750Address == 0x5C,
              "CLOCK_BH1750_ADDRESS must be 0x23 or 0x5C");
static_assert(CLOCK_ENABLE_DIAGNOSTICS == 0 ||
                  CLOCK_ENABLE_DIAGNOSTICS == 1,
              "CLOCK_ENABLE_DIAGNOSTICS must be 0 or 1");
static_assert(CLOCK_LIGHT_DIAGNOSTICS == 0 ||
                  CLOCK_LIGHT_DIAGNOSTICS == 1,
              "CLOCK_LIGHT_DIAGNOSTICS must be 0 or 1");
static_assert(CLOCK_LIGHT_DIAGNOSTICS == 0 ||
                  CLOCK_ENABLE_DIAGNOSTICS == 1,
              "CLOCK_LIGHT_DIAGNOSTICS requires CLOCK_ENABLE_DIAGNOSTICS");
static_assert(kBleFastAdvertisingMinMs >= 20 &&
                  kBleFastAdvertisingMinMs <=
                      kBleFastAdvertisingMaxMs &&
                  kBleFastAdvertisingMaxMs <= 10240,
              "Fast BLE advertising interval must be 20-10240 ms");
static_assert(kBleSlowAdvertisingMinMs >= 20 &&
                  kBleSlowAdvertisingMinMs <=
                      kBleSlowAdvertisingMaxMs &&
                  kBleSlowAdvertisingMaxMs <= 10240,
              "Slow BLE advertising interval must be 20-10240 ms");
static_assert(kBleResyncGraceMs >= 60000UL,
              "BLE resync grace must cover the bounded notification retries");

#if defined(CONFIG_IDF_TARGET_ESP32)
static_assert(kCpuFrequencyMhz == 0 || kCpuFrequencyMhz == 80 ||
                  kCpuFrequencyMhz == 160 || kCpuFrequencyMhz == 240,
              "Classic ESP32 CPU frequency must be 80, 160, or 240 MHz");
constexpr bool classicPinUsesFlash(const uint8_t pin) {
  return pin >= 6 && pin <= 11;
}
static_assert(!classicPinUsesFlash(kI2cSdaPin) &&
                  !classicPinUsesFlash(kI2cSclPin) &&
                  !classicPinUsesFlash(kRecoveryButtonPin) &&
                  kI2cSdaPin < 34 && kI2cSclPin < 34,
              "Classic ESP32 I2C/recovery pins are unsafe for this profile");
#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_TM1637
static_assert(!classicPinUsesFlash(kTm1637ClkPin) &&
                  !classicPinUsesFlash(kTm1637DioPin) &&
                  kTm1637ClkPin < 34 && kTm1637DioPin < 34,
              "TM1637 pins must be safe output-capable classic ESP32 GPIO");
#endif
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
static_assert(kCpuFrequencyMhz == 0 || kCpuFrequencyMhz == 80 ||
                  kCpuFrequencyMhz == 160,
              "ESP32-C3 CPU frequency must be 80 or 160 MHz");
constexpr bool c3ModulePinIsReserved(const uint8_t pin) {
  return pin == 2 || pin == 8 || pin == 9 || pin == 18 || pin == 19;
}
static_assert(!c3ModulePinIsReserved(kI2cSdaPin) &&
                  !c3ModulePinIsReserved(kI2cSclPin),
              "C3 I2C pins must avoid strapping and native-USB pins");
#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_TM1637
static_assert(!c3ModulePinIsReserved(kTm1637ClkPin) &&
                  !c3ModulePinIsReserved(kTm1637DioPin),
              "C3 display pins must avoid strapping and native-USB pins");
#endif
#endif
}  // namespace config
