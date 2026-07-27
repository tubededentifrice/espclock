#include "AppConfig.h"

#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_TM1637

#include "Tm1637DisplayBackend.h"

namespace {
constexpr uint8_t kPair[4] = {
    SEG_A | SEG_B | SEG_E | SEG_F | SEG_G,
    SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
    SEG_B | SEG_C,
    SEG_E | SEG_G,
};
constexpr uint8_t kSet[4] = {
    0,
    SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,
    SEG_D | SEG_E | SEG_F | SEG_G,
};
constexpr uint8_t kWifi[4] = {
    SEG_B | SEG_D | SEG_F,
    SEG_B | SEG_C,
    SEG_A | SEG_E | SEG_F | SEG_G,
    SEG_B | SEG_C,
};
constexpr uint8_t kDashes[4] = {SEG_G, SEG_G, SEG_G, SEG_G};
constexpr uint8_t kReset[4] = {
    SEG_E | SEG_G,
    SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
    SEG_D | SEG_E | SEG_F | SEG_G,
    0,
};
}  // namespace

Tm1637DisplayBackend::Tm1637DisplayBackend(const uint8_t clkPin,
                                           const uint8_t dioPin)
    : display_(clkPin, dioPin) {}

bool Tm1637DisplayBackend::begin() {
  setBrightness(0);
  display_.clear();
  return true;
}

void Tm1637DisplayBackend::setBrightness(const uint8_t level) {
  const uint8_t bounded = level > 7 ? 7 : level;
  if (bounded == brightness_) {
    return;
  }
  brightness_ = bounded;
  display_.setBrightness(bounded, true);
}

void Tm1637DisplayBackend::showTime(const uint8_t hour,
                                    const uint8_t minute,
                                    const uint8_t,
                                    const bool colonOn,
                                    const bool) {
  const uint32_t frameKey = 0x10000000UL |
                            (static_cast<uint32_t>(hour) << 16U) |
                            (static_cast<uint32_t>(minute) << 8U) |
                            static_cast<uint32_t>(colonOn);
  if (frameKey == lastFrameKey_) {
    return;
  }
  lastFrameKey_ = frameKey;
  const uint16_t hhmm = static_cast<uint16_t>(hour * 100U + minute);
  display_.showNumberDecEx(hhmm, colonOn ? 0x40 : 0x00, true, 4, 0);
}

void Tm1637DisplayBackend::showMessage(const DisplayMessage message) {
  const uint32_t frameKey =
      0x20000000UL | static_cast<uint8_t>(message);
  if (frameKey == lastFrameKey_) {
    return;
  }
  lastFrameKey_ = frameKey;
  switch (message) {
    case DisplayMessage::kPairing:
      display_.setSegments(kPair);
      break;
    case DisplayMessage::kPortal:
      display_.setSegments(kSet);
      break;
    case DisplayMessage::kWifi:
      display_.setSegments(kWifi);
      break;
    case DisplayMessage::kRecovery:
      display_.setSegments(kReset);
      break;
    case DisplayMessage::kNoTime:
    default:
      display_.setSegments(kDashes);
      break;
  }
}

#endif
