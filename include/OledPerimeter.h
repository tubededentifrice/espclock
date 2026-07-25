#pragma once

#include <stdint.h>

namespace oledperimeter {

struct Pixel {
  uint16_t x;
  uint16_t y;
};

struct LitSpan {
  uint16_t first;
  uint16_t count;
};

// A one-dimensional ordered-dither rank keeps thin perimeter strokes visible
// on every edge. At the minimum 2/16 coverage, one path pixel in every eight
// is retained instead of allowing a screen-space mask to erase a whole side.
constexpr uint8_t kDitherRank[16] = {
    0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};

inline bool isPixelVisible(const uint16_t index,
                           const uint8_t coverageSixteenths) {
  const uint8_t boundedCoverage =
      coverageSixteenths < 16U ? coverageSixteenths : 16U;
  return kDitherRank[index & 0x0FU] < boundedCoverage;
}

constexpr uint16_t pixelCount(const uint16_t width, const uint16_t height) {
  return width < 2U || height < 2U
             ? 0U
             : static_cast<uint16_t>(2U * width + 2U * height - 4U);
}

inline uint16_t filledPixelCount(const uint16_t width,
                                 const uint16_t height,
                                 const uint8_t elapsedSeconds) {
  const uint8_t boundedSeconds =
      elapsedSeconds > 60U ? 60U : elapsedSeconds;
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(pixelCount(width, height)) * boundedSeconds) /
      60U);
}

inline uint16_t filledPixelCountForSecond(const uint16_t width,
                                          const uint16_t height,
                                          const uint8_t second) {
  const uint8_t boundedSecond = second > 59U ? 59U : second;
  return filledPixelCount(width, height,
                          static_cast<uint8_t>(boundedSecond + 1U));
}

inline LitSpan litSpanForTime(const uint16_t width,
                              const uint16_t height,
                              const uint8_t minute,
                              const uint8_t second) {
  const uint16_t totalPixels = pixelCount(width, height);
  const uint16_t changedPixels =
      filledPixelCountForSecond(width, height, second);
  if ((minute & 1U) != 0U) {
    return {changedPixels,
            static_cast<uint16_t>(totalPixels - changedPixels)};
  }
  return {0U, changedPixels};
}

// Walk clockwise around the extreme pixels, starting at the top-left corner.
// Each corner appears exactly once.
inline Pixel pixelAt(const uint16_t width,
                     const uint16_t height,
                     uint16_t index) {
  if (index < width) {
    return {index, 0U};
  }
  index = static_cast<uint16_t>(index - width);
  if (index < height - 1U) {
    return {static_cast<uint16_t>(width - 1U),
            static_cast<uint16_t>(index + 1U)};
  }
  index = static_cast<uint16_t>(index - (height - 1U));
  if (index < width - 1U) {
    return {static_cast<uint16_t>(width - 2U - index),
            static_cast<uint16_t>(height - 1U)};
  }
  index = static_cast<uint16_t>(index - (width - 1U));
  return {0U, static_cast<uint16_t>(height - 2U - index)};
}

// Rotate the clockwise path so progress begins at the 12 o'clock position.
inline Pixel progressPixelAt(const uint16_t width,
                             const uint16_t height,
                             const uint16_t index) {
  const uint16_t totalPixels = pixelCount(width, height);
  if (totalPixels == 0U) {
    return {0U, 0U};
  }
  const uint16_t rotatedIndex = static_cast<uint16_t>(
      (index + width / 2U) % totalPixels);
  return pixelAt(width, height, rotatedIndex);
}

}  // namespace oledperimeter
