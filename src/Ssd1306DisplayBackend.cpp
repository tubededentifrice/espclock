#include "AppConfig.h"

#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_SSD1306

#include "Ssd1306DisplayBackend.h"

#include <Wire.h>

namespace {
constexpr uint8_t kSegmentMasks[10] = {
    0b00111111,  // 0: A B C D E F
    0b00000110,  // 1: B C
    0b01011011,  // 2: A B D E G
    0b01001111,  // 3: A B C D G
    0b01100110,  // 4: B C F G
    0b01101101,  // 5: A C D F G
    0b01111101,  // 6: A C D E F G
    0b00000111,  // 7: A B C
    0b01111111,  // 8
    0b01101111,  // 9
};
constexpr uint8_t kContrast[8] = {1, 2, 4, 8, 16, 32, 64, 128};

const char* messageText(const DisplayMessage message) {
  switch (message) {
    case DisplayMessage::kPairing:
      return "PAIR";
    case DisplayMessage::kPortal:
      return "SET";
    case DisplayMessage::kWifi:
      return "WIFI";
    case DisplayMessage::kRecovery:
      return "RESET";
    case DisplayMessage::kNoTime:
    default:
      return "----";
  }
}
}  // namespace

Ssd1306DisplayBackend::Ssd1306DisplayBackend(const uint8_t width,
                                             const uint8_t height,
                                             const uint8_t address)
    : display_(width, height, &Wire, -1),
      address_(address),
      width_(width),
      height_(height) {}

bool Ssd1306DisplayBackend::begin() {
  Wire.beginTransmission(address_);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  available_ =
      display_.begin(SSD1306_SWITCHCAPVCC, address_, false, false);
  if (!available_) {
    return false;
  }
  setBrightness(0);
  display_.clearDisplay();
  display_.display();
  return true;
}

void Ssd1306DisplayBackend::setBrightness(const uint8_t level) {
  const uint8_t bounded = level > 7 ? 7 : level;
  if (!available_ || bounded == brightness_) {
    return;
  }
  brightness_ = bounded;
  display_.ssd1306_command(SSD1306_SETCONTRAST);
  display_.ssd1306_command(kContrast[bounded]);
}

void Ssd1306DisplayBackend::drawDigit(const uint8_t digit,
                                      const int16_t x,
                                      const int16_t y,
                                      const int16_t width,
                                      const int16_t height) {
  if (digit > 9) {
    return;
  }
  const uint8_t mask = kSegmentMasks[digit];
  const int16_t thickness = height >= 48 ? 5 : 3;
  const int16_t half = height / 2;
  const int16_t horizontalWidth = width - 2 * thickness;
  const int16_t verticalHeight = half - 2 * thickness;

  if (mask & (1U << 0)) {
    display_.fillRoundRect(x + thickness, y, horizontalWidth, thickness,
                           thickness / 2, SSD1306_WHITE);
  }
  if (mask & (1U << 1)) {
    display_.fillRoundRect(x + width - thickness, y + thickness, thickness,
                           verticalHeight, thickness / 2, SSD1306_WHITE);
  }
  if (mask & (1U << 2)) {
    display_.fillRoundRect(x + width - thickness, y + half + thickness,
                           thickness, verticalHeight, thickness / 2,
                           SSD1306_WHITE);
  }
  if (mask & (1U << 3)) {
    display_.fillRoundRect(x + thickness, y + height - thickness,
                           horizontalWidth, thickness, thickness / 2,
                           SSD1306_WHITE);
  }
  if (mask & (1U << 4)) {
    display_.fillRoundRect(x, y + half + thickness, thickness, verticalHeight,
                           thickness / 2, SSD1306_WHITE);
  }
  if (mask & (1U << 5)) {
    display_.fillRoundRect(x, y + thickness, thickness, verticalHeight,
                           thickness / 2, SSD1306_WHITE);
  }
  if (mask & (1U << 6)) {
    display_.fillRoundRect(x + thickness, y + half - thickness / 2,
                           horizontalWidth, thickness, thickness / 2,
                           SSD1306_WHITE);
  }
}

void Ssd1306DisplayBackend::present() {
  if (available_) {
    display_.display();
  }
}

void Ssd1306DisplayBackend::showTime(const uint8_t hour,
                                     const uint8_t minute,
                                     const bool colonOn) {
  if (!available_) {
    return;
  }
  const uint32_t frameKey = 0x10000000UL |
                            (static_cast<uint32_t>(hour) << 16U) |
                            (static_cast<uint32_t>(minute) << 8U) |
                            static_cast<uint32_t>(colonOn);
  if (frameKey == lastFrameKey_) {
    return;
  }
  lastFrameKey_ = frameKey;
  display_.clearDisplay();

  constexpr int16_t kOuterMargin = 2;
  constexpr int16_t kColonWidth = 8;
  constexpr int16_t kDigitGap = 2;
  const int16_t digitWidth =
      (width_ - 2 * kOuterMargin - kColonWidth - 3 * kDigitGap) / 4;
  const int16_t digitHeight = height_ - 4;
  const int16_t y = 2;
  // Move the static digit pattern by up to two pixels every five minutes to
  // distribute long-term OLED wear without making the clock visibly wander.
  const int16_t wearShift =
      static_cast<int16_t>(((hour * 60U + minute) / 5U) % 3U);
  int16_t x = kOuterMargin + wearShift;
  const uint8_t digits[4] = {
      static_cast<uint8_t>(hour / 10U),
      static_cast<uint8_t>(hour % 10U),
      static_cast<uint8_t>(minute / 10U),
      static_cast<uint8_t>(minute % 10U),
  };
  drawDigit(digits[0], x, y, digitWidth, digitHeight);
  x += digitWidth + kDigitGap;
  drawDigit(digits[1], x, y, digitWidth, digitHeight);
  x += digitWidth + kDigitGap;
  if (colonOn) {
    const int16_t radius = height_ >= 48 ? 2 : 1;
    display_.fillCircle(x + kColonWidth / 2, height_ / 3, radius,
                        SSD1306_WHITE);
    display_.fillCircle(x + kColonWidth / 2, (height_ * 2) / 3, radius,
                        SSD1306_WHITE);
  }
  x += kColonWidth;
  drawDigit(digits[2], x, y, digitWidth, digitHeight);
  x += digitWidth + kDigitGap;
  drawDigit(digits[3], x, y, digitWidth, digitHeight);
  present();
}

void Ssd1306DisplayBackend::showMessage(const DisplayMessage message) {
  if (!available_) {
    return;
  }
  const uint32_t frameKey = 0x20000000UL | static_cast<uint8_t>(message);
  if (frameKey == lastFrameKey_) {
    return;
  }
  lastFrameKey_ = frameKey;
  const char* text = messageText(message);
  const uint8_t textSize = height_ >= 64 ? 3 : 2;
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t textWidth = 0;
  uint16_t textHeight = 0;
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
  display_.setTextSize(textSize);
  display_.setTextWrap(false);
  display_.getTextBounds(text, 0, 0, &x1, &y1, &textWidth, &textHeight);
  display_.setCursor((width_ - textWidth) / 2 - x1,
                     (height_ - textHeight) / 2 - y1);
  display_.print(text);
  present();
}

#endif
