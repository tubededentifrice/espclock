#pragma once

#include <stdint.h>

namespace oledglyph {

constexpr uint8_t kDigitRows[10][7] = {
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
    {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110},
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
    {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110},
    {0b01110, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b01110},
};

struct FaceGeometry {
  int16_t cellWidth;
  int16_t cellHeight;
  int16_t digitGap;
  int16_t digitWidth;
  int16_t digitHeight;
  int16_t colonWidth;
  int16_t contentWidth;
};

inline bool isPixelLit(const uint8_t digit,
                       const uint8_t row,
                       const uint8_t column) {
  return digit < 10U && row < 7U && column < 5U &&
         (kDigitRows[digit][row] & (1U << (4U - column))) != 0;
}

inline uint8_t litPixelCount(const uint8_t digit) {
  uint8_t count = 0;
  for (uint8_t row = 0; row < 7; ++row) {
    for (uint8_t column = 0; column < 5; ++column) {
      if (isPixelLit(digit, row, column)) {
        ++count;
      }
    }
  }
  return count;
}

inline FaceGeometry faceGeometry(const uint8_t displayHeight) {
  const int16_t cellWidth = displayHeight >= 48U ? 5 : 4;
  const int16_t cellHeight = displayHeight >= 48U ? 8 : 4;
  constexpr int16_t kDigitGap = 2;
  const int16_t digitWidth = 5 * cellWidth;
  const int16_t digitHeight = 7 * cellHeight;
  const int16_t colonWidth = displayHeight >= 48U ? 8 : 6;
  return {
      cellWidth,
      cellHeight,
      kDigitGap,
      digitWidth,
      digitHeight,
      colonWidth,
      static_cast<int16_t>(4 * digitWidth + colonWidth + 3 * kDigitGap),
  };
}

inline int16_t sparseCoordinate(const uint8_t index,
                                const uint8_t maximumIndex,
                                const int16_t extent) {
  if (maximumIndex == 0U || extent <= 1) {
    return 0;
  }
  const uint8_t boundedIndex =
      index < maximumIndex ? index : maximumIndex;
  // Spread the sparse points from the first through the last pixel of the
  // daytime glyph extent so night mode changes stroke density, not size.
  return static_cast<int16_t>(
      (static_cast<int32_t>(boundedIndex) * (extent - 1)) / maximumIndex);
}

}  // namespace oledglyph
