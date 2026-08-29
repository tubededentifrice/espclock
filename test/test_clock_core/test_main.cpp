#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "ClockCore.h"
#include "CaptivePortalAutofillCore.h"
#include "OledDigitGlyph.h"
#include "OledBrightness.h"
#include "OledPerimeter.h"
#include "PortalPage.h"

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

void test_portal_time_form_is_fixed_capacity_and_exact() {
  int64_t epoch = 0;
  int16_t offset = 0;
  const char normal[] = "epoch=1784970000&offset=240";
  const char reversed[] = "offset=-300&epoch=1784970000";
  const char duplicate[] =
      "epoch=1784970000&epoch=1784970001&offset=240";
  const char unknown[] = "epoch=1784970000&offset=240&extra=1";
  TEST_ASSERT_TRUE(clockcore::parsePortalTimeForm(
      reinterpret_cast<const uint8_t*>(normal), sizeof(normal) - 1,
      epoch, offset));
  TEST_ASSERT_EQUAL_INT64(1784970000LL, epoch);
  TEST_ASSERT_EQUAL_INT16(240, offset);
  TEST_ASSERT_TRUE(clockcore::parsePortalTimeForm(
      reinterpret_cast<const uint8_t*>(reversed), sizeof(reversed) - 1,
      epoch, offset));
  TEST_ASSERT_EQUAL_INT16(-300, offset);
  TEST_ASSERT_FALSE(clockcore::parsePortalTimeForm(
      reinterpret_cast<const uint8_t*>(duplicate), sizeof(duplicate) - 1,
      epoch, offset));
  TEST_ASSERT_FALSE(clockcore::parsePortalTimeForm(
      reinterpret_cast<const uint8_t*>(unknown), sizeof(unknown) - 1,
      epoch, offset));
  uint8_t oversized[clockcore::kMaximumPortalTimeFormLength + 1] = {};
  memset(oversized, '1', sizeof(oversized));
  TEST_ASSERT_FALSE(clockcore::parsePortalTimeForm(
      oversized, sizeof(oversized), epoch, offset));
}

void test_portal_page_submits_device_time_automatically() {
  TEST_ASSERT_NOT_NULL(strstr(
      portalpage::kHtml,
      "document.addEventListener('DOMContentLoaded',()=>setTime()"));
  TEST_ASSERT_NOT_NULL(strstr(portalpage::kHtml, "Date.now()"));
  TEST_ASSERT_NOT_NULL(
      strstr(portalpage::kHtml, "new Date().getTimezoneOffset()"));
  TEST_ASSERT_NOT_NULL(strstr(portalpage::kHtml, "maxAttempts=3"));
  TEST_ASSERT_NULL(strstr(portalpage::kHtml, "<button"));
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

void test_sync_routes_and_source_specific_intervals() {
  TEST_ASSERT_FALSE(clockcore::isValidSyncRouteValue(0));
  TEST_ASSERT_TRUE(clockcore::isValidSyncRouteValue(1));
  TEST_ASSERT_TRUE(clockcore::isValidSyncRouteValue(3));
  TEST_ASSERT_FALSE(clockcore::isValidSyncRouteValue(4));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SyncRoute::kBle),
      static_cast<uint8_t>(
          clockcore::syncRouteForSource(TimeSource::kBle)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SyncRoute::kPortal),
      static_cast<uint8_t>(
          clockcore::syncRouteForSource(TimeSource::kPortal)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SyncRoute::kNtp),
      static_cast<uint8_t>(
          clockcore::syncRouteForSource(TimeSource::kNtp)));

  constexpr int64_t kLastSync = 1784970000LL;
  constexpr uint32_t kBleInterval = 6UL * 60UL * 60UL * 1000UL;
  constexpr uint32_t kWifiInterval = 24UL * 60UL * 60UL * 1000UL;
  TEST_ASSERT_EQUAL_UINT32(
      5UL * 60UL * 60UL * 1000UL,
      clockcore::remainingResyncDelayMs(
          SyncRoute::kBle, kLastSync, kLastSync + 3600,
          kBleInterval, kWifiInterval));
  TEST_ASSERT_EQUAL_UINT32(
      23UL * 60UL * 60UL * 1000UL,
      clockcore::remainingResyncDelayMs(
          SyncRoute::kPortal, kLastSync, kLastSync + 3600,
          kBleInterval, kWifiInterval));
  TEST_ASSERT_EQUAL_UINT32(
      0, clockcore::remainingResyncDelayMs(
             SyncRoute::kNtp, kLastSync, kLastSync + 86400,
             kBleInterval, kWifiInterval));
  TEST_ASSERT_EQUAL_UINT32(
      0, clockcore::remainingResyncDelayMs(
             SyncRoute::kBle, kLastSync, 0,
             kBleInterval, kWifiInterval));
}

void test_persisted_sync_trust_requires_a_complete_valid_record() {
  constexpr int64_t kLastSync = 1784970000LL;
  TEST_ASSERT_TRUE(clockcore::isValidPersistedSyncState(
      true, kLastSync, static_cast<uint8_t>(SyncRoute::kBle), 240));
  TEST_ASSERT_FALSE(clockcore::isValidPersistedSyncState(
      false, kLastSync, static_cast<uint8_t>(SyncRoute::kBle), 240));
  TEST_ASSERT_FALSE(clockcore::isValidPersistedSyncState(
      true, 0, static_cast<uint8_t>(SyncRoute::kBle), 240));
  TEST_ASSERT_FALSE(clockcore::isValidPersistedSyncState(
      true, kLastSync, static_cast<uint8_t>(SyncRoute::kUnselected), 240));
  TEST_ASSERT_FALSE(clockcore::isValidPersistedSyncState(
      true, kLastSync, 0xFF, 240));
  TEST_ASSERT_FALSE(clockcore::isValidPersistedSyncState(
      true, kLastSync, static_cast<uint8_t>(SyncRoute::kBle), 841));
}

void test_persisted_sync_record_is_atomic_and_detects_corruption() {
  uint8_t record[clockcore::kPersistedSyncRecordSize] = {};
  TEST_ASSERT_TRUE(clockcore::encodePersistedSyncState(
      1784970000LL, SyncRoute::kPortal, -300, record, sizeof(record)));
  int64_t epoch = 0;
  SyncRoute route = SyncRoute::kUnselected;
  int16_t offset = 0;
  TEST_ASSERT_TRUE(clockcore::decodePersistedSyncState(
      record, sizeof(record), epoch, route, offset));
  TEST_ASSERT_EQUAL_INT64(1784970000LL, epoch);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncRoute::kPortal),
                          static_cast<uint8_t>(route));
  TEST_ASSERT_EQUAL_INT16(-300, offset);

  record[6] ^= 0x01U;
  TEST_ASSERT_FALSE(clockcore::decodePersistedSyncState(
      record, sizeof(record), epoch, route, offset));
  TEST_ASSERT_FALSE(clockcore::decodePersistedSyncState(
      record, sizeof(record) - 1U, epoch, route, offset));
}

void test_pending_time_updates_keep_the_highest_priority_source() {
  TEST_ASSERT_TRUE(clockcore::shouldReplacePendingTimeUpdate(
      false, TimeSource::kRtc, TimeSource::kNtp));
  TEST_ASSERT_TRUE(clockcore::shouldReplacePendingTimeUpdate(
      true, TimeSource::kNtp, TimeSource::kPortal));
  TEST_ASSERT_TRUE(clockcore::shouldReplacePendingTimeUpdate(
      true, TimeSource::kPortal, TimeSource::kBle));
  TEST_ASSERT_TRUE(clockcore::shouldReplacePendingTimeUpdate(
      true, TimeSource::kBle, TimeSource::kBle));
  TEST_ASSERT_FALSE(clockcore::shouldReplacePendingTimeUpdate(
      true, TimeSource::kBle, TimeSource::kPortal));
  TEST_ASSERT_FALSE(clockcore::shouldReplacePendingTimeUpdate(
      true, TimeSource::kPortal, TimeSource::kNtp));

  // A lower-priority callback can run after dequeue and before its network
  // producer stops. The applied source must discard that late update, but it
  // must preserve a later higher-priority update.
  TEST_ASSERT_TRUE(clockcore::shouldDiscardQueuedTimeUpdate(
      true, TimeSource::kNtp, TimeSource::kBle));
  TEST_ASSERT_TRUE(clockcore::shouldDiscardQueuedTimeUpdate(
      true, TimeSource::kPortal, TimeSource::kPortal));
  TEST_ASSERT_FALSE(clockcore::shouldDiscardQueuedTimeUpdate(
      true, TimeSource::kBle, TimeSource::kNtp));
  TEST_ASSERT_FALSE(clockcore::shouldDiscardQueuedTimeUpdate(
      false, TimeSource::kNtp, TimeSource::kBle));
}

void test_monotonic_intervals_are_wrap_safe() {
  constexpr uint32_t kStarted = UINT32_MAX - 10U;
  TEST_ASSERT_FALSE(clockcore::monotonicIntervalElapsed(
      18U, kStarted, 30U));
  TEST_ASSERT_TRUE(clockcore::monotonicIntervalElapsed(
      19U, kStarted, 30U));
}

void test_monotonic_epoch_is_independent_of_system_clock_changes() {
  constexpr int64_t kBaseline = 1784970000LL;
  constexpr uint64_t kStartedUs = 900000000000ULL;
  TEST_ASSERT_EQUAL_INT64(
      kBaseline + 125,
      clockcore::extrapolateMonotonicEpoch(
          kBaseline, kStartedUs, kStartedUs + 125999999ULL));
  TEST_ASSERT_EQUAL_INT64(
      0, clockcore::extrapolateMonotonicEpoch(
             kBaseline, kStartedUs, kStartedUs - 1ULL));
  TEST_ASSERT_EQUAL_INT64(
      0, clockcore::extrapolateMonotonicEpoch(
             clockcore::kMaximumValidEpoch, kStartedUs,
             kStartedUs + 1000000ULL));
}

void test_initial_sync_phase_boundaries() {
  constexpr uint32_t kBleOnlyMs = 10000UL;
  constexpr uint32_t kSetupMs = 120000UL;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::InitialSyncPhase::kBleOnly),
      static_cast<uint8_t>(
          clockcore::initialSyncPhase(9999, kBleOnlyMs, kSetupMs)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::InitialSyncPhase::kPortal),
      static_cast<uint8_t>(
          clockcore::initialSyncPhase(10000, kBleOnlyMs, kSetupMs)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::InitialSyncPhase::kPortal),
      static_cast<uint8_t>(
          clockcore::initialSyncPhase(119999, kBleOnlyMs, kSetupMs)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::InitialSyncPhase::kNtp),
      static_cast<uint8_t>(
          clockcore::initialSyncPhase(120000, kBleOnlyMs, kSetupMs)));
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
  TEST_ASSERT_FALSE(
      oledbrightness::showsSyncOverdueIndicator(0, true));
  TEST_ASSERT_TRUE(
      oledbrightness::showsSyncOverdueIndicator(1, true));
  TEST_ASSERT_FALSE(
      oledbrightness::showsSyncOverdueIndicator(7, false));
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

void test_pairing_frame_stays_visible_through_initial_setup() {
  clockcore::DisplayFrame frame = clockcore::makeDisplayFrame(
      1000, true, true, UserDisplayState::kPairing, 1500);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::DisplayContent::kPairing),
      static_cast<uint8_t>(frame.content));

  frame = clockcore::makeDisplayFrame(
      2000, true, true, UserDisplayState::kPairing, 1500);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::DisplayContent::kPairing),
      static_cast<uint8_t>(frame.content));

  frame = clockcore::makeDisplayFrame(
      2000, false, true, UserDisplayState::kPairing, 1500);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(clockcore::DisplayContent::kPairing),
      static_cast<uint8_t>(frame.content));
}

void test_overdue_refresh_never_interrupts_valid_clock() {
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

  state = clockcore::selectUserDisplayState(
      false, true, false, false, true, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UserDisplayState::kPairing),
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

void test_wifi_candidates_break_equal_rssi_ties_by_bssid() {
  clockcore::WifiCandidateRanker firstOrder;
  clockcore::WifiCandidateRanker reverseOrder;
  const uint8_t lowerBssid[6] = {0x30, 0x41, 0x52, 0x63, 0x74, 0x01};
  const uint8_t higherBssid[6] = {0x30, 0x41, 0x52, 0x63, 0x74, 0x02};

  TEST_ASSERT_TRUE(
      firstOrder.consider("Higher", higherBssid, 1, -50));
  TEST_ASSERT_TRUE(firstOrder.consider("Lower", lowerBssid, 1, -50));
  TEST_ASSERT_TRUE(reverseOrder.consider("Lower", lowerBssid, 1, -50));
  TEST_ASSERT_TRUE(
      reverseOrder.consider("Higher", higherBssid, 1, -50));

  TEST_ASSERT_EQUAL_STRING("Lower", firstOrder.at(0)->ssid);
  TEST_ASSERT_EQUAL_STRING("Higher", firstOrder.at(1)->ssid);
  TEST_ASSERT_EQUAL_STRING(firstOrder.at(0)->ssid,
                           reverseOrder.at(0)->ssid);
  TEST_ASSERT_EQUAL_STRING(firstOrder.at(1)->ssid,
                           reverseOrder.at(1)->ssid);
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

void test_captive_portal_identity_uses_reserved_contact_data() {
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(0x12345678U, identity);
  TEST_ASSERT_EQUAL_STRING("345678", identity.nonce);
  TEST_ASSERT_EQUAL_STRING("clock345678", identity.username);
  TEST_ASSERT_EQUAL_STRING("clock345678@example.com", identity.email);
  TEST_ASSERT_EQUAL_STRING("+12025550196", identity.phone);
}

void test_captive_portal_ntp_failure_policy_is_single_attempt() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(
          captiveportal::NtpFailureAction::kTryPortal),
      static_cast<uint8_t>(
          captiveportal::actionAfterNtpFailure(true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(
          captiveportal::NtpFailureAction::kFailCandidate),
      static_cast<uint8_t>(
          captiveportal::actionAfterNtpFailure(true, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(
          captiveportal::NtpFailureAction::kFailCandidate),
      static_cast<uint8_t>(
          captiveportal::actionAfterNtpFailure(false, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(
          captiveportal::NtpFailureAction::kFailCandidate),
      static_cast<uint8_t>(
          captiveportal::actionAfterNtpFailure(true, false, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(captiveportal::PortalResultAction::kRetryNtp),
      static_cast<uint8_t>(captiveportal::actionAfterPortalResult(
          captiveportal::AutomationResult::kPortalOpened)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(
          captiveportal::PortalResultAction::kFailCandidate),
      static_cast<uint8_t>(captiveportal::actionAfterPortalResult(
          captiveportal::AutomationResult::kInternetAlreadyOpen)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(
          captiveportal::PortalTimeoutAction::kTryNextCandidate),
      static_cast<uint8_t>(
          captiveportal::actionAfterPortalTimeout(false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(
          captiveportal::PortalTimeoutAction::kFinishWindow),
      static_cast<uint8_t>(
          captiveportal::actionAfterPortalTimeout(true)));
}

void test_captive_portal_redirect_timeout_and_generation_limits() {
  uint8_t redirects = 0;
  for (uint8_t index = 0; index < captiveportal::kMaximumRedirects; ++index) {
    TEST_ASSERT_TRUE(captiveportal::consumeRedirect(redirects));
  }
  TEST_ASSERT_EQUAL_UINT8(captiveportal::kMaximumRedirects, redirects);
  TEST_ASSERT_FALSE(captiveportal::consumeRedirect(redirects));

  constexpr uint32_t kStarted = UINT32_MAX - 10U;
  TEST_ASSERT_FALSE(
      captiveportal::automationWindowExpired(18U, kStarted, 30U));
  TEST_ASSERT_TRUE(
      captiveportal::automationWindowExpired(19U, kStarted, 30U));
  TEST_ASSERT_TRUE(captiveportal::completionMatchesGeneration(7, 7));
  TEST_ASSERT_FALSE(captiveportal::completionMatchesGeneration(8, 7));
}

void test_captive_portal_cookie_jar_is_bounded_and_origin_scoped() {
  captiveportal::CookieJar cookies;
  TEST_ASSERT_TRUE(cookies.add(
      "portal.example", "session=one; Path=/; HttpOnly"));
  TEST_ASSERT_TRUE(cookies.add("portal.example", "csrf=two; Secure"));
  TEST_ASSERT_TRUE(cookies.add("other.example", "foreign=three"));
  TEST_ASSERT_TRUE(cookies.add("portal.example", "session=replaced"));
  TEST_ASSERT_EQUAL_UINT8(3, cookies.count());
  char header[128] = {};
  TEST_ASSERT_TRUE(
      cookies.headerFor("portal.example", header, sizeof(header)));
  TEST_ASSERT_EQUAL_STRING("session=replaced; csrf=two", header);
  TEST_ASSERT_TRUE(
      cookies.headerFor("unrelated.example", header, sizeof(header)));
  TEST_ASSERT_EQUAL_STRING("", header);
  TEST_ASSERT_FALSE(cookies.add("portal.example", "bad\r\n=value"));

  captiveportal::CookieJar aggregateLimit;
  char largePair[captiveportal::kMaximumCookiePairLength + 1] = {};
  for (uint8_t index = 0; index < 4; ++index) {
    largePair[0] = static_cast<char>('a' + index);
    largePair[1] = '=';
    memset(largePair + 2, 'x', sizeof(largePair) - 3);
    largePair[sizeof(largePair) - 1] = '\0';
    TEST_ASSERT_TRUE(aggregateLimit.add("portal.example", largePair));
  }
  TEST_ASSERT_EQUAL_UINT32(captiveportal::kMaximumCookieBytes,
                           aggregateLimit.bytes());
  TEST_ASSERT_FALSE(aggregateLimit.add("portal.example", "e=1"));
}

void test_captive_portal_reuses_identity_across_form_steps() {
  constexpr char kFirstStep[] =
      "<form action='/step2' method=post><input name=email required>"
      "<button>Continue</button></form>";
  constexpr char kSecondStep[] =
      "<form action='/finish' method=post><input name=first_name required>"
      "<input name=email required><button>Accept</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(0x123456U, identity);
  captiveportal::Submission first;
  captiveportal::Submission second;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kFirstStep, strlen(kFirstStep), "http://portal.example/start",
      identity, first));
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kSecondStep, strlen(kSecondStep), "http://portal.example/step2",
      identity, second));
  TEST_ASSERT_EQUAL_STRING(first.fields[0].value, second.fields[1].value);
  TEST_ASSERT_EQUAL_STRING("Travel", second.fields[0].value);
}

void test_captive_portal_url_resolution_and_origin_checks() {
  char resolved[captiveportal::kMaximumUrlLength + 1] = {};
  TEST_ASSERT_TRUE(captiveportal::resolveUrl(
      "http://portal.example/start/index.html?old=1", "../accept?x=1",
      resolved, sizeof(resolved)));
  TEST_ASSERT_EQUAL_STRING(
      "http://portal.example/start/../accept?x=1", resolved);
  TEST_ASSERT_TRUE(captiveportal::sameOrigin(
      "http://portal.example/start", resolved));
  TEST_ASSERT_FALSE(captiveportal::sameOrigin(
      "http://portal.example/start", "https://portal.example/accept"));
  TEST_ASSERT_FALSE(captiveportal::resolveUrl(
      "http://portal.example/start", "javascript:accept()", resolved,
      sizeof(resolved)));
  captiveportal::Url unsafe;
  TEST_ASSERT_FALSE(captiveportal::parseUrl(
      "http://portal.example/\r\nInjected: yes", unsafe));
}

void test_captive_portal_builds_button_only_submission() {
  constexpr char kHtml[] =
      "<html><form action='/accept' method='post'>"
      "<input type='hidden' name='token' value='a&amp;b'>"
      "<button name='commit' value='yes'>Accept and connect</button>"
      "</form></html>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(7, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kHtml, strlen(kHtml), "http://portal.example/start", identity,
      submission));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(captiveportal::Method::kPost),
      static_cast<uint8_t>(submission.method));
  TEST_ASSERT_EQUAL_STRING("http://portal.example/accept",
                           submission.action);
  TEST_ASSERT_EQUAL_UINT8(2, submission.fieldCount);
  TEST_ASSERT_EQUAL_STRING("token", submission.fields[0].name);
  TEST_ASSERT_EQUAL_STRING("a&b", submission.fields[0].value);
  TEST_ASSERT_EQUAL_STRING("commit", submission.fields[1].name);
  TEST_ASSERT_EQUAL_STRING("yes", submission.fields[1].value);
}

void test_captive_portal_preserves_repeated_hidden_fields() {
  constexpr char kHtml[] =
      "<form action='/accept' method='post'>"
      "<input type=hidden name=token value=one>"
      "<input type=hidden name=token value=two>"
      "<button>Accept and connect</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(7, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kHtml, strlen(kHtml), "http://portal.example/start", identity,
      submission));
  TEST_ASSERT_EQUAL_UINT8(2, submission.fieldCount);
  TEST_ASSERT_EQUAL_STRING("one", submission.fields[0].value);
  TEST_ASSERT_EQUAL_STRING("two", submission.fields[1].value);
}

void test_captive_portal_submits_a_truly_fieldless_button_form() {
  constexpr char kHtml[] =
      "<form action='/accept' method='post'>"
      "<button>Accept terms and connect</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(7, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kHtml, strlen(kHtml), "http://portal.example/start", identity,
      submission));
  TEST_ASSERT_EQUAL_UINT8(0, submission.fieldCount);
  TEST_ASSERT_EQUAL_STRING("http://portal.example/accept",
                           submission.action);
  char encoded[captiveportal::kMaximumRequestBodyLength + 1] = {};
  TEST_ASSERT_TRUE(captiveportal::encodeSubmission(
      submission, encoded, sizeof(encoded)));
  TEST_ASSERT_EQUAL_STRING("", encoded);
}

void test_captive_portal_fills_common_fields_and_terms() {
  constexpr char kHtml[] =
      "<form method=post action='join'>"
      "<input name=first_name required>"
      "<input name=last_name required>"
      "<input type=email name=contact required>"
      "<input type=tel name=mobile>"
      "<input type=checkbox name=terms value=accepted>"
      "<input type=radio name=plan value=free>"
      "<input type=radio name=plan value=paid>"
      "<select name=country><option value=''>Choose</option>"
      "<option value=US selected>United States</option></select>"
      "<button>Continue</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(0xABCDEF, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kHtml, strlen(kHtml), "https://portal.example/welcome/", identity,
      submission));
  TEST_ASSERT_EQUAL_STRING("https://portal.example/welcome/join",
                           submission.action);
  TEST_ASSERT_EQUAL_UINT8(7, submission.fieldCount);
  TEST_ASSERT_EQUAL_STRING("Travel", submission.fields[0].value);
  TEST_ASSERT_EQUAL_STRING("Clock", submission.fields[1].value);
  TEST_ASSERT_EQUAL_STRING("clockabcdef@example.com",
                           submission.fields[2].value);
  TEST_ASSERT_EQUAL_STRING("+12025550175", submission.fields[3].value);
  TEST_ASSERT_EQUAL_STRING("accepted", submission.fields[4].value);
  TEST_ASSERT_EQUAL_STRING("free", submission.fields[5].value);
  TEST_ASSERT_EQUAL_STRING("US", submission.fields[6].value);
}

void test_captive_portal_recognizes_valued_boolean_attributes() {
  constexpr char kHtml[] =
      "<form method=post action='/join'>"
      "<input name=access_code required='required'>"
      "<input type=checkbox name=marketing value=yes disabled='disabled'>"
      "<button>Continue</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(0xABCDEF, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kHtml, strlen(kHtml), "https://portal.example/welcome", identity,
      submission));
  TEST_ASSERT_EQUAL_UINT8(1, submission.fieldCount);
  TEST_ASSERT_EQUAL_STRING("access_code", submission.fields[0].name);
  TEST_ASSERT_EQUAL_STRING("Guest", submission.fields[0].value);
}

void test_captive_portal_checks_only_explicit_terms_boxes() {
  constexpr char kOptionalMarketing[] =
      "<form method=post action='/join'>"
      "<input type=hidden name=token value=one>"
      "<input type=checkbox name=accept_marketing value=yes>"
      "<input type=checkbox name=photos value=yes>"
      "<input type=checkbox name=accept_photos value=yes>"
      "<input type=checkbox name=accept_terms value=yes>"
      "<button>Continue</button></form>";
  constexpr char kRequiredUnknown[] =
      "<form method=post action='/join'>"
      "<input type=checkbox name=marketing value=yes required>"
      "<button>Continue</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(0xABCDEF, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kOptionalMarketing, strlen(kOptionalMarketing),
      "https://portal.example/welcome", identity, submission));
  TEST_ASSERT_EQUAL_UINT8(2, submission.fieldCount);
  TEST_ASSERT_EQUAL_STRING("token", submission.fields[0].name);
  TEST_ASSERT_EQUAL_STRING("accept_terms", submission.fields[1].name);
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kRequiredUnknown, strlen(kRequiredUnknown),
      "https://portal.example/welcome", identity, submission));
}

void test_captive_portal_does_not_select_marketing_radio_choices() {
  constexpr char kOptionalMarketing[] =
      "<form method=post action='/join'>"
      "<input type=hidden name=token value=one>"
      "<input type=radio name=newsletter value=yes>"
      "<input type=radio name=newsletter value=no>"
      "<button>Continue</button></form>";
  constexpr char kRequiredMarketing[] =
      "<form method=post action='/join'>"
      "<input type=radio name=marketing value=yes required>"
      "<input type=radio name=marketing value=no required>"
      "<button>Continue</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(0xABCDEF, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kOptionalMarketing, strlen(kOptionalMarketing),
      "https://portal.example/welcome", identity, submission));
  TEST_ASSERT_EQUAL_UINT8(1, submission.fieldCount);
  TEST_ASSERT_EQUAL_STRING("token", submission.fields[0].name);
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kRequiredMarketing, strlen(kRequiredMarketing),
      "https://portal.example/welcome", identity, submission));
}

void test_captive_portal_prefers_free_guest_form() {
  constexpr char kHtml[] =
      "<form action='/paid'><input name=email required>"
      "<button>Purchase premium access</button></form>"
      "<form action='/guest'><input name=email required>"
      "<button>Free guest access</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(1, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kHtml, strlen(kHtml), "http://portal.example/", identity,
      submission));
  TEST_ASSERT_EQUAL_STRING("http://portal.example/guest",
                           submission.action);
}

void test_captive_portal_rejects_credential_and_payment_forms() {
  constexpr char kPassword[] =
      "<form><input name=user required><input type=password name=secret>"
      "<button>Login</button></form>";
  constexpr char kPayment[] =
      "<form><input name=credit_card required><button>Continue</button>"
      "</form>";
  constexpr char kPaidButton[] =
      "<form><input name=email required>"
      "<button>Purchase premium access</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(1, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kPassword, strlen(kPassword), "http://portal.example/", identity,
      submission));
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kPayment, strlen(kPayment), "http://portal.example/", identity,
      submission));
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kPaidButton, strlen(kPaidButton), "http://portal.example/", identity,
      submission));
}

void test_captive_portal_rejects_cross_origin_and_unsupported_actions() {
  constexpr char kCrossOrigin[] =
      "<form action='https://other.example/accept'>"
      "<input type=hidden name=token value=1><button>Accept</button></form>";
  constexpr char kJavascript[] =
      "<form action='javascript:submitNow()'>"
      "<input type=hidden name=token value=1><button>Accept</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(1, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kCrossOrigin, strlen(kCrossOrigin), "https://portal.example/",
      identity, submission));
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kJavascript, strlen(kJavascript), "https://portal.example/",
      identity, submission));
}

void test_captive_portal_encodes_form_without_growth() {
  constexpr char kHtml[] =
      "<form action='/join' method=post>"
      "<input type=hidden name='csrf token' value='a&b'>"
      "<input name=full_name required><button>Accept</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(1, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_TRUE(captiveportal::buildSubmission(
      kHtml, strlen(kHtml), "http://portal.example/", identity,
      submission));
  char encoded[captiveportal::kMaximumRequestBodyLength + 1] = {};
  TEST_ASSERT_TRUE(captiveportal::encodeSubmission(
      submission, encoded, sizeof(encoded)));
  TEST_ASSERT_EQUAL_STRING(
      "csrf+token=a%26b&full_name=Travel+Clock", encoded);
  char tooSmall[8] = {};
  TEST_ASSERT_FALSE(captiveportal::encodeSubmission(
      submission, tooSmall, sizeof(tooSmall)));
}

void test_captive_portal_rejects_malformed_or_excessive_forms() {
  constexpr char kMalformed[] =
      "<form><input name=x required";
  constexpr char kTooManyForms[] =
      "<form><button>Accept</button></form>"
      "<form><button>Accept</button></form>"
      "<form><button>Accept</button></form>"
      "<form><button>Accept</button></form>"
      "<form><button>Accept</button></form>";
  constexpr char kOversizedValue[] =
      "<form><input type=hidden name=token value='"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "a'><button>Accept</button></form>";
  constexpr char kTooManyControls[] =
      "<form><input disabled><input disabled><input disabled><input disabled>"
      "<input disabled><input disabled><input disabled><input disabled>"
      "<input disabled><input disabled><input disabled><input disabled>"
      "<input disabled><input disabled><input disabled><input disabled>"
      "<input disabled><input disabled><input disabled><input disabled>"
      "<input disabled><input disabled><input disabled><input disabled>"
      "<input disabled><button>Accept</button></form>";
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(1, identity);
  captiveportal::Submission submission;
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kMalformed, strlen(kMalformed), "http://portal.example/", identity,
      submission));
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kTooManyForms, strlen(kTooManyForms), "http://portal.example/",
      identity, submission));
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kOversizedValue, strlen(kOversizedValue), "http://portal.example/",
      identity, submission));
  TEST_ASSERT_FALSE(captiveportal::buildSubmission(
      kTooManyControls, strlen(kTooManyControls), "http://portal.example/",
      identity, submission));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_epoch_validation);
  RUN_TEST(test_text_time_payload);
  RUN_TEST(test_binary_time_payload_with_negative_offset);
  RUN_TEST(test_payload_rejects_bad_values);
  RUN_TEST(test_portal_time_form_is_fixed_capacity_and_exact);
  RUN_TEST(test_portal_page_submits_device_time_automatically);
  RUN_TEST(test_time_correction_requires_fresh_candidate);
  RUN_TEST(test_sync_routes_and_source_specific_intervals);
  RUN_TEST(test_persisted_sync_trust_requires_a_complete_valid_record);
  RUN_TEST(test_persisted_sync_record_is_atomic_and_detects_corruption);
  RUN_TEST(test_pending_time_updates_keep_the_highest_priority_source);
  RUN_TEST(test_monotonic_intervals_are_wrap_safe);
  RUN_TEST(test_monotonic_epoch_is_independent_of_system_clock_changes);
  RUN_TEST(test_initial_sync_phase_boundaries);
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
  RUN_TEST(test_pairing_frame_stays_visible_through_initial_setup);
  RUN_TEST(test_overdue_refresh_never_interrupts_valid_clock);
  RUN_TEST(test_boot_setup_statuses_remain_visible);
  RUN_TEST(test_display_frame_colon_signals_sync_quality);
  RUN_TEST(test_bssid_backoff_is_per_radio_not_ssid);
  RUN_TEST(test_bssid_backoff_has_a_hard_capacity);
  RUN_TEST(test_wifi_candidates_are_bounded_and_ranked_without_duplicates);
  RUN_TEST(test_wifi_candidates_break_equal_rssi_ties_by_bssid);
  RUN_TEST(test_wifi_candidate_rejects_invalid_metadata);
  RUN_TEST(test_captive_portal_identity_uses_reserved_contact_data);
  RUN_TEST(test_captive_portal_ntp_failure_policy_is_single_attempt);
  RUN_TEST(test_captive_portal_redirect_timeout_and_generation_limits);
  RUN_TEST(test_captive_portal_cookie_jar_is_bounded_and_origin_scoped);
  RUN_TEST(test_captive_portal_reuses_identity_across_form_steps);
  RUN_TEST(test_captive_portal_url_resolution_and_origin_checks);
  RUN_TEST(test_captive_portal_builds_button_only_submission);
  RUN_TEST(test_captive_portal_preserves_repeated_hidden_fields);
  RUN_TEST(test_captive_portal_submits_a_truly_fieldless_button_form);
  RUN_TEST(test_captive_portal_fills_common_fields_and_terms);
  RUN_TEST(test_captive_portal_recognizes_valued_boolean_attributes);
  RUN_TEST(test_captive_portal_checks_only_explicit_terms_boxes);
  RUN_TEST(test_captive_portal_does_not_select_marketing_radio_choices);
  RUN_TEST(test_captive_portal_prefers_free_guest_form);
  RUN_TEST(test_captive_portal_rejects_credential_and_payment_forms);
  RUN_TEST(test_captive_portal_rejects_cross_origin_and_unsupported_actions);
  RUN_TEST(test_captive_portal_encodes_form_without_growth);
  RUN_TEST(test_captive_portal_rejects_malformed_or_excessive_forms);
  return UNITY_END();
}
