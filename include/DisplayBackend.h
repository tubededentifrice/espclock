#pragma once

#include <stdint.h>

enum class DisplayMessage : uint8_t {
  kPairing,
  kPortal,
  kWifi,
  kNoTime,
  kRecovery,
};

class DisplayBackend {
 public:
  virtual ~DisplayBackend() = default;
  virtual bool begin() = 0;
  virtual void setBrightness(uint8_t level) = 0;
  virtual void showTime(uint8_t hour, uint8_t minute, uint8_t second,
                        bool colonOn) = 0;
  virtual void showMessage(DisplayMessage message) = 0;
  virtual const char* name() const = 0;
};

DisplayBackend& selectedDisplayBackend();
