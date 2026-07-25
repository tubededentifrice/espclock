#pragma once

#include <TM1637Display.h>

#include "DisplayBackend.h"

class Tm1637DisplayBackend final : public DisplayBackend {
 public:
  Tm1637DisplayBackend(uint8_t clkPin, uint8_t dioPin);
  bool begin() override;
  void setBrightness(uint8_t level) override;
  void showTime(uint8_t hour, uint8_t minute, bool colonOn) override;
  void showMessage(DisplayMessage message) override;
  const char* name() const override { return "TM1637"; }

 private:
  TM1637Display display_;
  uint8_t brightness_ = 0xFF;
};
