#include "AppConfig.h"

#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_SSD1306

#include "Ssd1306DisplayBackend.h"

#include <Arduino.h>
#include <Wire.h>

#include "Diagnostics.h"
#include "OledDigitGlyph.h"
#include "OledBrightness.h"
#include "OledPerimeter.h"

namespace {
constexpr uint8_t kBayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

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
  const uint8_t bounded = oledbrightness::boundedLevel(level);
  if (!available_ || bounded == brightness_) {
    return;
  }
  brightness_ = bounded;
  display_.ssd1306_command(SSD1306_SETCONTRAST);
  display_.ssd1306_command(oledbrightness::contrast(bounded));
  // Spatial dimming is part of the rendered frame, so force the current
  // content to be regenerated after every brightness-level change.
  lastFrameKey_ = 0xFFFFFFFFUL;
#if CLOCK_LIGHT_DIAGNOSTICS
  CLOCK_DIAGNOSTIC_PRINTF(
      "LIGHT oled_level=%u contrast=%u pixel_coverage=%u/16\n",
      static_cast<unsigned>(bounded),
      static_cast<unsigned>(oledbrightness::contrast(bounded)),
      static_cast<unsigned>(oledbrightness::ditherThreshold(bounded)));
#endif
}

void Ssd1306DisplayBackend::drawDigit(const uint8_t digit,
                                      const int16_t x,
                                      const int16_t y,
                                      const int16_t width,
                                      const int16_t height) {
  if (digit > 9) {
    return;
  }
  const int16_t cellWidth = width / 5;
  const int16_t cellHeight = height / 7;
  for (uint8_t row = 0; row < 7; ++row) {
    for (uint8_t column = 0; column < 5; ++column) {
      if (oledglyph::isPixelLit(digit, row, column)) {
        display_.fillRect(x + column * cellWidth, y + row * cellHeight,
                          cellWidth, cellHeight, SSD1306_WHITE);
      }
    }
  }
}

void Ssd1306DisplayBackend::drawNightDigit(const uint8_t digit,
                                           const int16_t x,
                                           const int16_t y,
                                           const int16_t width,
                                           const int16_t height) {
  if (digit > 9) {
    return;
  }
  for (uint8_t row = 0; row < 7; ++row) {
    for (uint8_t column = 0; column < 5; ++column) {
      if (oledglyph::isPixelLit(digit, row, column)) {
        display_.drawPixel(
            x + oledglyph::sparseCoordinate(column, 4, width),
            y + oledglyph::sparseCoordinate(row, 6, height),
            SSD1306_WHITE);
      }
    }
  }
}

void Ssd1306DisplayBackend::drawNightTime(const uint8_t hour,
                                          const uint8_t minute) {
  const oledglyph::FaceGeometry geometry =
      oledglyph::faceGeometry(height_);
  const int16_t y = (height_ - geometry.digitHeight) / 2;
  const int16_t wearShift =
      static_cast<int16_t>(((hour * 60U + minute) / 5U) % 3U) - 1;
  int16_t x = (width_ - geometry.contentWidth) / 2 + wearShift;
  const uint8_t digits[4] = {
      static_cast<uint8_t>(hour / 10U),
      static_cast<uint8_t>(hour % 10U),
      static_cast<uint8_t>(minute / 10U),
      static_cast<uint8_t>(minute % 10U),
  };

  drawNightDigit(digits[0], x, y, geometry.digitWidth, geometry.digitHeight);
  x += geometry.digitWidth + geometry.digitGap;
  drawNightDigit(digits[1], x, y, geometry.digitWidth, geometry.digitHeight);
  x += geometry.digitWidth + geometry.digitGap;
  display_.drawPixel(x + geometry.colonWidth / 2, height_ / 3,
                     SSD1306_WHITE);
  display_.drawPixel(x + geometry.colonWidth / 2, (height_ * 2) / 3,
                     SSD1306_WHITE);
  x += geometry.colonWidth;
  drawNightDigit(digits[2], x, y, geometry.digitWidth, geometry.digitHeight);
  x += geometry.digitWidth + geometry.digitGap;
  drawNightDigit(digits[3], x, y, geometry.digitWidth, geometry.digitHeight);
}

void Ssd1306DisplayBackend::present() {
  if (available_) {
    applyBrightnessDither();
    display_.display();
  }
}

void Ssd1306DisplayBackend::applyBrightnessDither() {
  const uint8_t threshold =
      oledbrightness::ditherThreshold(brightness_);
  if (threshold >= 16) {
    return;
  }
  uint8_t* buffer = display_.getBuffer();
  if (buffer == nullptr) {
    return;
  }
  for (uint8_t y = 0; y < height_; ++y) {
    const uint8_t pixelMask = static_cast<uint8_t>(1U << (y & 7U));
    const uint16_t rowOffset =
        static_cast<uint16_t>(y / 8U) * width_;
    for (uint8_t x = 0; x < width_; ++x) {
      uint8_t& pixels = buffer[rowOffset + x];
      if ((pixels & pixelMask) != 0 &&
          kBayer4x4[y & 3U][x & 3U] >= threshold) {
        pixels &= static_cast<uint8_t>(~pixelMask);
      }
    }
  }
}

void Ssd1306DisplayBackend::drawPerimeterProgress(
    const uint8_t minute,
    const uint8_t second) {
  const oledperimeter::LitSpan litSpan =
      oledperimeter::litSpanForTime(width_, height_, minute, second);
  const uint8_t coverage =
      oledbrightness::ditherThreshold(brightness_);
  const uint16_t endIndex =
      static_cast<uint16_t>(litSpan.first + litSpan.count);
  for (uint16_t index = litSpan.first; index < endIndex; ++index) {
    if (!oledperimeter::isPixelVisible(index, coverage)) {
      continue;
    }
    const oledperimeter::Pixel pixel =
        oledperimeter::progressPixelAt(width_, height_, index);
    display_.drawPixel(pixel.x, pixel.y, SSD1306_WHITE);
  }
}

void Ssd1306DisplayBackend::showTime(const uint8_t hour,
                                     const uint8_t minute,
                                     const uint8_t second,
                                     const bool) {
  if (!available_) {
    return;
  }
  const uint32_t frameKey = 0x10000000UL |
                            (static_cast<uint32_t>(hour) << 16U) |
                            (static_cast<uint32_t>(minute) << 8U) |
                            static_cast<uint32_t>(second);
  if (frameKey == lastFrameKey_) {
    return;
  }
  lastFrameKey_ = frameKey;
  display_.clearDisplay();

  if (oledbrightness::usesSparseNightFace(brightness_)) {
    // Each source-font pixel becomes one widely spaced OLED pixel. Do not
    // dither this already sparse face or its digit strokes become ambiguous.
    // Omitting the perimeter also removes decorative light in a dark bedroom.
    drawNightTime(hour, minute);
    display_.display();
    return;
  }

  const oledglyph::FaceGeometry geometry =
      oledglyph::faceGeometry(height_);
  const int16_t y = (height_ - geometry.digitHeight) / 2;
  // Move the static digit pattern by one pixel either way every five minutes to
  // distribute long-term OLED wear without making the clock visibly wander.
  const int16_t wearShift =
      static_cast<int16_t>(((hour * 60U + minute) / 5U) % 3U) - 1;
  int16_t x = (width_ - geometry.contentWidth) / 2 + wearShift;
  const uint8_t digits[4] = {
      static_cast<uint8_t>(hour / 10U),
      static_cast<uint8_t>(hour % 10U),
      static_cast<uint8_t>(minute / 10U),
      static_cast<uint8_t>(minute % 10U),
  };
  drawDigit(digits[0], x, y, geometry.digitWidth, geometry.digitHeight);
  x += geometry.digitWidth + geometry.digitGap;
  drawDigit(digits[1], x, y, geometry.digitWidth, geometry.digitHeight);
  x += geometry.digitWidth + geometry.digitGap;
  const int16_t radius = height_ >= 48 ? 2 : 1;
  display_.fillCircle(x + geometry.colonWidth / 2, height_ / 3, radius,
                      SSD1306_WHITE);
  display_.fillCircle(x + geometry.colonWidth / 2,
                      (height_ * 2) / 3, radius,
                      SSD1306_WHITE);
  x += geometry.colonWidth;
  drawDigit(digits[2], x, y, geometry.digitWidth, geometry.digitHeight);
  x += geometry.digitWidth + geometry.digitGap;
  drawDigit(digits[3], x, y, geometry.digitWidth, geometry.digitHeight);

  // Dither the broad glyphs in screen space, then sample the one-pixel border
  // along its path. Applying the 2D mask to the border can reject every pixel
  // on an edge (notably x=127 and y=63 at low coverage).
  applyBrightnessDither();
  drawPerimeterProgress(minute, second);
  display_.display();
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
