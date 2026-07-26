#pragma once

#include "AppConfig.h"

#if CLOCK_ENABLE_DIAGNOSTICS
#include <Arduino.h>

#define CLOCK_DIAGNOSTIC_BEGIN(baud) Serial.begin(baud)
#define CLOCK_DIAGNOSTIC_PRINTF(...) Serial.printf(__VA_ARGS__)
#define CLOCK_DIAGNOSTIC_PRINTLN(message) Serial.println(message)
#else
#define CLOCK_DIAGNOSTIC_BEGIN(baud) ((void)0)
#define CLOCK_DIAGNOSTIC_PRINTF(...) ((void)0)
#define CLOCK_DIAGNOSTIC_PRINTLN(message) ((void)0)
#endif
