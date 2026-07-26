#pragma once

#include <stdint.h>

namespace oledbrightness {

constexpr uint8_t kLevelCount = 8;
constexpr uint8_t kContrast[kLevelCount] = {
    1, 4, 12, 28, 56, 96, 160, 255};
constexpr uint8_t kDitherThreshold[kLevelCount] = {
    1, 4, 6, 8, 10, 12, 14, 16};

inline uint8_t boundedLevel(const uint8_t level) {
  return level < kLevelCount ? level : kLevelCount - 1;
}

inline uint8_t contrast(const uint8_t level) {
  return kContrast[boundedLevel(level)];
}

inline uint8_t ditherThreshold(const uint8_t level) {
  return kDitherThreshold[boundedLevel(level)];
}

inline bool usesSparseNightFace(const uint8_t level) {
  return boundedLevel(level) == 0;
}

}  // namespace oledbrightness
