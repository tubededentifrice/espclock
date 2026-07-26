#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "DisplayBackend.h"

class Ssd1306DisplayBackend final : public DisplayBackend {
 public:
  Ssd1306DisplayBackend(uint8_t width, uint8_t height, uint8_t address);
  bool begin() override;
  void setBrightness(uint8_t level) override;
  void showTime(uint8_t hour, uint8_t minute, uint8_t second,
                bool colonOn) override;
  void showMessage(DisplayMessage message) override;
  const char* name() const override { return "SSD1306"; }

 private:
  void applyBrightnessDither();
  void drawDigit(uint8_t digit, int16_t x, int16_t y,
                 int16_t width, int16_t height);
  void drawNightDigit(uint8_t digit, int16_t x, int16_t y,
                      int16_t width, int16_t height);
  void drawNightTime(uint8_t hour, uint8_t minute);
  void drawPerimeterProgress(uint8_t minute, uint8_t second);
  void present();

  Adafruit_SSD1306 display_;
  uint8_t address_;
  uint8_t width_;
  uint8_t height_;
  bool available_ = false;
  uint8_t brightness_ = 0xFF;
  uint32_t lastFrameKey_ = 0xFFFFFFFFUL;
};
