#include <stdio.h>

#include <unity.h>

#include "ClockCore.h"
#include "OledDigitGlyph.h"
#include "OledBrightness.h"
#include "OledPerimeter.h"

void test_epoch_validation() {
  TEST_ASSERT_FALSE(clockcore::isValidEpoch(1704067199LL));
  TEST_ASSERT_TRUE(clockcore::isValidEpoch(1704067200LL));
  TEST_ASSERT_TRUE(clockcore::isValidEpoch(4102444799LL));
  TEST_ASSERT_FALSE(clockcore::isValidEpoch(4102444800LL));
}

void test_text_time_payload() {
  const char value[] = "1784970000,240";
  int64_t epoch = 0;
  int16_t offset = 0;
  TEST_ASSERT_TRUE(clockcore::parseTimeSyncPayload(
      reinterpret_cast<const uint8_t*>(value), sizeof(value) - 1,
      epoch, offset));
  TEST_ASSERT_EQUAL_INT64(1784970000LL, epoch);
  TEST_ASSERT_EQUAL_INT16(240, offset);
}

void test_binary_time_payload_with_negative_offset() {
  const uint8_t value[12] = {
      1, 0x90, 0x33, 0x68, 0x6A, 0, 0, 0, 0, 0xD4, 0xFE, 0};
  int64_t epoch = 0;
  int16_t offset = 0;
  TEST_ASSERT_TRUE(
      clockcore::parseTimeSyncPayload(value, sizeof(value), epoch, offset));
  TEST_ASSERT_EQUAL_INT64(1785213840LL, epoch);
  TEST_ASSERT_EQUAL_INT16(-300, offset);
}

void test_payload_rejects_bad_values() {
  int64_t epoch = 0;
  int16_t offset = 0;
  const char oldTime[] = "123,0";
  const char badOffset[] = "1784970000,900";
  const char trailing[] = "1784970000,240oops";
  TEST_ASSERT_FALSE(clockcore::parseTimeSyncPayload(
      reinterpret_cast<const uint8_t*>(oldTime), sizeof(oldTime) - 1,
      epoch, offset));
  TEST_ASSERT_FALSE(clockcore::parseTimeSyncPayload(
      reinterpret_cast<const uint8_t*>(badOffset), sizeof(badOffset) - 1,
      epoch, offset));
  TEST_ASSERT_FALSE(clockcore::parseTimeSyncPayload(
      reinterpret_cast<const uint8_t*>(trailing), sizeof(trailing) - 1,
      epoch, offset));
}

void test_time_correction_requires_fresh_candidate() {
  TEST_ASSERT_TRUE(clockcore::isPlausibleCorrection(0, 1784970000LL));
  TEST_ASSERT_TRUE(
      clockcore::isPlausibleCorrection(1784970000LL, 1784970300LL));
  TEST_ASSERT_TRUE(
      clockcore::isPlausibleCorrection(1784970000LL, 1784969700LL));
  TEST_ASSERT_FALSE(
      clockcore::isPlausibleCorrection(1784970000LL, 1784970301LL));
  TEST_ASSERT_FALSE(
      clockcore::isPlausibleCorrection(1784970000LL, 1784969699LL));
}

void test_unconfirmed_rtc_allows_first_large_correction() {
  TEST_ASSERT_TRUE(clockcore::isAcceptableCorrection(
      false, 1704067200LL, 1784970000LL));
  // A stored trust marker can outlive the running clock when no RTC is fitted.
  // A valid first sync after power loss must still recover the clock.
  TEST_ASSERT_TRUE(clockcore::isAcceptableCorrection(
      true, 0, 1784970000LL));
  TEST_ASSERT_TRUE(clockcore::isAcceptableCorrection(
      true, 1784970000LL, 1784970300LL));
  TEST_ASSERT_FALSE(clockcore::isAcceptableCorrection(
      true, 1704067200LL, 1784970000LL));
}

void test_light_filter_is_smooth_and_bounded() {
  clockcore::LightLevelController light;
  TEST_ASSERT_EQUAL_UINT8(0, light.update(0.1F));
  for (int i = 0; i < 60; ++i) {
    light.update(2000.0F);
  }
  TEST_ASSERT_EQUAL_UINT8(7, light.level());
  const uint8_t brightLevel = light.level();
  light.update(-1.0F);
  TEST_ASSERT_EQUAL_UINT8(brightLevel, light.level());
  for (int i = 0; i < 100; ++i) {
    light.update(0.83F);
  }
  TEST_ASSERT_EQUAL_UINT8(0, light.level());
}

void test_light_filter_reset_discards_stale_brightness() {
  clockcore::LightLevelController light;
  for (int i = 0; i < 60; ++i) {
    light.update(2000.0F);
  }
  TEST_ASSERT_EQUAL_UINT8(7, light.level());
  light.reset();
  TEST_ASSERT_EQUAL_UINT8(0, light.level());
  TEST_ASSERT_EQUAL_UINT8(0, light.update(0.1F));
}

void test_light_levels_cover_the_full_ambient_range() {
  constexpr float kRepresentativeLux[8] = {
      0.83F, 2.0F, 5.0F, 20.0F, 70.0F, 250.0F, 800.0F, 2000.0F};
  for (uint8_t expectedLevel = 0; expectedLevel < 8; ++expectedLevel) {
    clockcore::LightLevelController light;
    TEST_ASSERT_EQUAL_UINT8(
        expectedLevel, light.update(kRepresentativeLux[expectedLevel]));
  }
}

void test_oled_brightness_steps_are_monotonic_and_bounded() {
  TEST_ASSERT_EQUAL_UINT8(1, oledbrightness::contrast(0));
  TEST_ASSERT_EQUAL_UINT8(1, oledbrightness::ditherThreshold(0));
  TEST_ASSERT_TRUE(oledbrightness::usesSparseNightFace(0));
  TEST_ASSERT_FALSE(oledbrightness::usesSparseNightFace(1));
  for (uint8_t level = 1; level < oledbrightness::kLevelCount; ++level) {
    TEST_ASSERT_GREATER_THAN(
        oledbrightness::contrast(level - 1),
        oledbrightness::contrast(level));
    TEST_ASSERT_GREATER_THAN(
        oledbrightness::ditherThreshold(level - 1),
        oledbrightness::ditherThreshold(level));
  }
  TEST_ASSERT_EQUAL_UINT8(
      oledbrightness::contrast(7), oledbrightness::contrast(255));
  TEST_ASSERT_EQUAL_UINT8(
      oledbrightness::ditherThreshold(7),
      oledbrightness::ditherThreshold(255));
}

void test_sparse_oled_night_face_matches_day_geometry() {
  for (uint8_t digit = 0; digit < 10; ++digit) {
    TEST_ASSERT_GREATER_THAN_UINT8(9, oledglyph::litPixelCount(digit));
    TEST_ASSERT_LESS_THAN_UINT8(21, oledglyph::litPixelCount(digit));
  }
  TEST_ASSERT_EQUAL_UINT8(0, oledglyph::litPixelCount(10));
  TEST_ASSERT_FALSE(oledglyph::isPixelLit(0, 7, 0));
  TEST_ASSERT_FALSE(oledglyph::isPixelLit(0, 0, 5));

  const oledglyph::FaceGeometry tall = oledglyph::faceGeometry(64);
  TEST_ASSERT_EQUAL_INT16(114, tall.contentWidth);
  TEST_ASSERT_EQUAL_INT16(56, tall.digitHeight);
  TEST_ASSERT_LESS_OR_EQUAL_INT16(128, tall.contentWidth);
  TEST_ASSERT_LESS_OR_EQUAL_INT16(128, tall.contentWidth + 2);
  TEST_ASSERT_LESS_OR_EQUAL_INT16(64, tall.digitHeight);
  TEST_ASSERT_EQUAL_INT16(
      0, oledglyph::sparseCoordinate(0, 4, tall.digitWidth));
  TEST_ASSERT_EQUAL_INT16(
      24, oledglyph::sparseCoordinate(4, 4, tall.digitWidth));
  TEST_ASSERT_EQUAL_INT16(
      55, oledglyph::sparseCoordinate(6, 6, tall.digitHeight));

  const oledglyph::FaceGeometry shortDisplay =
      oledglyph::faceGeometry(32);
  TEST_ASSERT_EQUAL_INT16(92, shortDisplay.contentWidth);
  TEST_ASSERT_EQUAL_INT16(28, shortDisplay.digitHeight);
  TEST_ASSERT_LESS_OR_EQUAL_INT16(128, shortDisplay.contentWidth);
  TEST_ASSERT_LESS_OR_EQUAL_INT16(128, shortDisplay.contentWidth + 2);
  TEST_ASSERT_LESS_OR_EQUAL_INT16(32, shortDisplay.digitHeight);
  TEST_ASSERT_EQUAL_INT16(
      19, oledglyph::sparseCoordinate(4, 4, shortDisplay.digitWidth));
  TEST_ASSERT_EQUAL_INT16(
      27, oledglyph::sparseCoordinate(6, 6, shortDisplay.digitHeight));
}

void test_oled_perimeter_uses_each_extreme_pixel_once() {
  TEST_ASSERT_EQUAL_UINT16(380, oledperimeter::pixelCount(128, 64));
  TEST_ASSERT_EQUAL_UINT16(316, oledperimeter::pixelCount(128, 32));

  oledperimeter::Pixel pixel = oledperimeter::pixelAt(128, 64, 0);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.y);
  pixel = oledperimeter::pixelAt(128, 64, 127);
  TEST_ASSERT_EQUAL_UINT16(127, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.y);
  pixel = oledperimeter::pixelAt(128, 64, 190);
  TEST_ASSERT_EQUAL_UINT16(127, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(63, pixel.y);
  pixel = oledperimeter::pixelAt(128, 64, 317);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(63, pixel.y);
  pixel = oledperimeter::pixelAt(128, 64, 379);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(1, pixel.y);
}

void test_oled_perimeter_progress_starts_at_top_center() {
  oledperimeter::Pixel pixel =
      oledperimeter::progressPixelAt(128, 64, 0);
  TEST_ASSERT_EQUAL_UINT16(64, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.y);
  pixel = oledperimeter::progressPixelAt(128, 64, 63);
  TEST_ASSERT_EQUAL_UINT16(127, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.y);
  pixel = oledperimeter::progressPixelAt(128, 64, 64);
  TEST_ASSERT_EQUAL_UINT16(127, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(1, pixel.y);
  pixel = oledperimeter::progressPixelAt(128, 64, 379);
  TEST_ASSERT_EQUAL_UINT16(63, pixel.x);
  TEST_ASSERT_EQUAL_UINT16(0, pixel.y);
}

void test_oled_perimeter_dither_keeps_every_edge_visible() {
  uint16_t edgeCounts[4] = {};
  uint16_t visibleCount = 0;
  const uint16_t totalPixels = oledperimeter::pixelCount(128, 64);
  for (uint16_t index = 0; index < totalPixels; ++index) {
    if (!oledperimeter::isPixelVisible(index, 1)) {
      continue;
    }
    ++visibleCount;
    const oledperimeter::Pixel pixel =
        oledperimeter::progressPixelAt(128, 64, index);
    if (pixel.y == 0) {
      ++edgeCounts[0];
    }
    if (pixel.x == 127) {
      ++edgeCounts[1];
    }
    if (pixel.y == 63) {
      ++edgeCounts[2];
    }
    if (pixel.x == 0) {
      ++edgeCounts[3];
    }
  }

  TEST_ASSERT_EQUAL_UINT16(24, visibleCount);
  for (uint8_t edge = 0; edge < 4; ++edge) {
    TEST_ASSERT_GREATER_THAN_UINT16(0, edgeCounts[edge]);
  }
}

void test_oled_perimeter_dither_is_monotonic_and_full_at_level_seven() {
  for (uint16_t index = 0; index < 380; ++index) {
    for (uint8_t coverage = 1; coverage < 16; ++coverage) {
      if (oledperimeter::isPixelVisible(index, coverage)) {
        TEST_ASSERT_TRUE(
            oledperimeter::isPixelVisible(index, coverage + 1));
      }
    }
    TEST_ASSERT_TRUE(oledperimeter::isPixelVisible(index, 16));
    TEST_ASSERT_FALSE(oledperimeter::isPixelVisible(index, 0));
  }
}

void test_oled_perimeter_fills_in_sixty_steps() {
  TEST_ASSERT_EQUAL_UINT16(
      0, oledperimeter::filledPixelCount(128, 64, 0));
  TEST_ASSERT_EQUAL_UINT16(
      6, oledperimeter::filledPixelCount(128, 64, 1));
  TEST_ASSERT_EQUAL_UINT16(
      190, oledperimeter::filledPixelCount(128, 64, 30));
  TEST_ASSERT_EQUAL_UINT16(
      373, oledperimeter::filledPixelCount(128, 64, 59));
  TEST_ASSERT_EQUAL_UINT16(
      380, oledperimeter::filledPixelCount(128, 64, 60));
  TEST_ASSERT_EQUAL_UINT16(
      6, oledperimeter::filledPixelCountForSecond(128, 64, 0));
  TEST_ASSERT_EQUAL_UINT16(
      380, oledperimeter::filledPixelCountForSecond(128, 64, 59));
}

void test_oled_perimeter_alternates_fill_and_drain_minutes() {
  oledperimeter::LitSpan span =
      oledperimeter::litSpanForTime(128, 64, 2, 0);
  TEST_ASSERT_EQUAL_UINT16(0, span.first);
  TEST_ASSERT_EQUAL_UINT16(6, span.count);
  span = oledperimeter::litSpanForTime(128, 64, 2, 59);
  TEST_ASSERT_EQUAL_UINT16(0, span.first);
  TEST_ASSERT_EQUAL_UINT16(380, span.count);

  span = oledperimeter::litSpanForTime(128, 64, 3, 0);
  TEST_ASSERT_EQUAL_UINT16(6, span.first);
  TEST_ASSERT_EQUAL_UINT16(374, span.count);
  span = oledperimeter::litSpanForTime(128, 64, 3, 59);
  TEST_ASSERT_EQUAL_UINT16(380, span.first);
  TEST_ASSERT_EQUAL_UINT16(0, span.count);
}

void test_display_frame_alternates_status_and_valid_time() {
  clockcore::DisplayFrame frame = clockcore::makeDisplayFrame(
      1000, true, true, UserDisplayState::kPairing, 1500);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::DisplayContent::kPairing),
      static_cast<uint8_t>(frame.content));

  frame = clockcore::makeDisplayFrame(
      2000, true, true, UserDisplayState::kPairing, 1500);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::DisplayContent::kTime),
      static_cast<uint8_t>(frame.content));

  frame = clockcore::makeDisplayFrame(
      2000, false, true, UserDisplayState::kPairing, 1500);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::DisplayContent::kPairing),
      static_cast<uint8_t>(frame.content));
}

void test_background_refresh_never_interrupts_valid_clock() {
  UserDisplayState state = clockcore::selectUserDisplayState(
      false, true, false, true, true, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kClock),
                          static_cast<uint8_t>(state));

  state = clockcore::selectUserDisplayState(
      false, false, true, true, true, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kClock),
                          static_cast<uint8_t>(state));

  state = clockcore::selectUserDisplayState(
      false, false, false, true, true, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kClock),
                          static_cast<uint8_t>(state));

  state = clockcore::selectUserDisplayState(
      false, false, true, false, false, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kWifi),
                          static_cast<uint8_t>(state));

  state = clockcore::selectUserDisplayState(
      true, false, true, true, true, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kRecovery),
                          static_cast<uint8_t>(state));
}

void test_boot_setup_statuses_remain_visible() {
  UserDisplayState state = clockcore::selectUserDisplayState(
      false, true, false, true, false, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kPortal),
                          static_cast<uint8_t>(state));

  state = clockcore::selectUserDisplayState(
      false, false, true, true, false, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kWifi),
                          static_cast<uint8_t>(state));

  state = clockcore::selectUserDisplayState(
      false, false, false, true, true, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kPairing),
                          static_cast<uint8_t>(state));

  state = clockcore::selectUserDisplayState(
      false, false, false, false, false, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kNoTime),
                          static_cast<uint8_t>(state));
}

void test_display_frame_colon_signals_sync_quality() {
  clockcore::DisplayFrame frame = clockcore::makeDisplayFrame(
      250, true, true, UserDisplayState::kClock, 1500);
  TEST_ASSERT_TRUE(frame.colonOn);
  frame = clockcore::makeDisplayFrame(
      500, true, true, UserDisplayState::kClock, 1500);
  TEST_ASSERT_FALSE(frame.colonOn);
  frame = clockcore::makeDisplayFrame(
      999, true, true, UserDisplayState::kClock, 1500);
  TEST_ASSERT_FALSE(frame.colonOn);
  frame = clockcore::makeDisplayFrame(
      1000, true, true, UserDisplayState::kClock, 1500);
  TEST_ASSERT_TRUE(frame.colonOn);
  frame = clockcore::makeDisplayFrame(
      100, true, false, UserDisplayState::kClock, 1500);
  TEST_ASSERT_TRUE(frame.colonOn);
  frame = clockcore::makeDisplayFrame(
      500, true, false, UserDisplayState::kClock, 1500);
  TEST_ASSERT_FALSE(frame.colonOn);
}

void test_bssid_backoff_is_per_radio_not_ssid() {
  clockcore::BssidAttemptTracker tracker;
  const uint8_t failedCafeAp[6] = {0x10, 0x22, 0x33, 0x44, 0x55, 0x01};
  const uint8_t otherCafeAp[6] = {0x10, 0x22, 0x33, 0x44, 0x55, 0x02};
  TEST_ASSERT_TRUE(tracker.add(failedCafeAp));
  TEST_ASSERT_TRUE(tracker.contains(failedCafeAp));
  TEST_ASSERT_FALSE(tracker.contains(otherCafeAp));
  TEST_ASSERT_FALSE(tracker.add(failedCafeAp));
  TEST_ASSERT_EQUAL_UINT8(1, tracker.size());
}

void test_bssid_backoff_has_a_hard_capacity() {
  clockcore::BssidAttemptTracker tracker;
  for (uint8_t i = 0; i < clockcore::BssidAttemptTracker::kCapacity; ++i) {
    const uint8_t bssid[6] = {0x20, 0x31, 0x42, 0x53, 0x64, i};
    TEST_ASSERT_TRUE(tracker.add(bssid));
  }
  const uint8_t overflow[6] = {0x20, 0x31, 0x42, 0x53, 0x64, 0xFF};
  TEST_ASSERT_FALSE(tracker.add(overflow));
  TEST_ASSERT_TRUE(tracker.full());
  TEST_ASSERT_EQUAL_UINT8(clockcore::BssidAttemptTracker::kCapacity,
                          tracker.size());
}

void test_wifi_candidates_are_bounded_and_ranked_without_duplicates() {
  clockcore::WifiCandidateRanker ranker;
  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t bssid[6] = {0x30, 0x41, 0x52, 0x63, 0x74, i};
    char ssid[12] = {};
    snprintf(ssid, sizeof(ssid), "Open-%u", i);
    ranker.consider(ssid, bssid, 1 + (i % 11), -90 + i * 5);
  }
  TEST_ASSERT_EQUAL_UINT8(clockcore::WifiCandidateRanker::kCapacity,
                          ranker.size());
  TEST_ASSERT_EQUAL_STRING("Open-7", ranker.at(0)->ssid);
  TEST_ASSERT_EQUAL_INT32(-55, ranker.at(0)->rssi);
  TEST_ASSERT_EQUAL_STRING("Open-2", ranker.at(5)->ssid);
  TEST_ASSERT_EQUAL_INT32(-80, ranker.at(5)->rssi);

  const uint8_t duplicate[6] = {0x30, 0x41, 0x52, 0x63, 0x74, 7};
  TEST_ASSERT_FALSE(ranker.consider("Weaker duplicate", duplicate, 6, -70));
  TEST_ASSERT_TRUE(ranker.consider("Stronger duplicate", duplicate, 6, -40));
  TEST_ASSERT_EQUAL_UINT8(clockcore::WifiCandidateRanker::kCapacity,
                          ranker.size());
  TEST_ASSERT_EQUAL_STRING("Stronger duplicate", ranker.at(0)->ssid);
  TEST_ASSERT_EQUAL_INT32(-40, ranker.at(0)->rssi);
}

void test_wifi_candidate_rejects_invalid_metadata() {
  clockcore::WifiCandidateRanker ranker;
  const uint8_t bssid[6] = {0x40, 0x51, 0x62, 0x73, 0x84, 0x95};
  const char tooLong[] = "123456789012345678901234567890123";
  TEST_ASSERT_FALSE(ranker.consider("", bssid, 1, -50));
  TEST_ASSERT_FALSE(ranker.consider(tooLong, bssid, 1, -50));
  TEST_ASSERT_FALSE(ranker.consider("Open", nullptr, 1, -50));
  TEST_ASSERT_FALSE(ranker.consider("Open", bssid, 0, -50));
  TEST_ASSERT_FALSE(ranker.consider("Open", bssid, 15, -50));
  TEST_ASSERT_EQUAL_UINT8(0, ranker.size());
  TEST_ASSERT_NULL(ranker.at(0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_epoch_validation);
  RUN_TEST(test_text_time_payload);
  RUN_TEST(test_binary_time_payload_with_negative_offset);
  RUN_TEST(test_payload_rejects_bad_values);
  RUN_TEST(test_time_correction_requires_fresh_candidate);
  RUN_TEST(test_unconfirmed_rtc_allows_first_large_correction);
  RUN_TEST(test_light_filter_is_smooth_and_bounded);
  RUN_TEST(test_light_filter_reset_discards_stale_brightness);
  RUN_TEST(test_light_levels_cover_the_full_ambient_range);
  RUN_TEST(test_oled_brightness_steps_are_monotonic_and_bounded);
  RUN_TEST(test_sparse_oled_night_face_matches_day_geometry);
  RUN_TEST(test_oled_perimeter_uses_each_extreme_pixel_once);
  RUN_TEST(test_oled_perimeter_progress_starts_at_top_center);
  RUN_TEST(test_oled_perimeter_dither_keeps_every_edge_visible);
  RUN_TEST(test_oled_perimeter_dither_is_monotonic_and_full_at_level_seven);
  RUN_TEST(test_oled_perimeter_fills_in_sixty_steps);
  RUN_TEST(test_oled_perimeter_alternates_fill_and_drain_minutes);
  RUN_TEST(test_display_frame_alternates_status_and_valid_time);
  RUN_TEST(test_background_refresh_never_interrupts_valid_clock);
  RUN_TEST(test_boot_setup_statuses_remain_visible);
  RUN_TEST(test_display_frame_colon_signals_sync_quality);
  RUN_TEST(test_bssid_backoff_is_per_radio_not_ssid);
  RUN_TEST(test_bssid_backoff_has_a_hard_capacity);
  RUN_TEST(test_wifi_candidates_are_bounded_and_ranked_without_duplicates);
  RUN_TEST(test_wifi_candidate_rejects_invalid_metadata);
  return UNITY_END();
}
