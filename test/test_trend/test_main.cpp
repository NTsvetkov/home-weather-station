#include <unity.h>

#include "trend.h"

static const uint32_t START  = 1794161162UL;
static const uint32_t MINUTE = 60UL;

void setUp() {}
void tearDown() {}

// Same windows and threshold as the firmware defaults.
static OutdoorTrends makeTrends() {
  return OutdoorTrends(30 * MINUTE, 180 * MINUTE, 1.0f);
}

static void feedFalling(OutdoorTrends& trends, uint32_t minute) {
  trends.update(START + minute * MINUTE, true,
                10.0f - 0.02f * minute, 90.0f - 0.1f * minute, 1020.0f - 0.02f * minute);
}

static void assertTrends(const OutdoorTrends& trends, int temperature, int humidity, int pressure) {
  TEST_ASSERT_EQUAL_INT(temperature, trends.temperature());
  TEST_ASSERT_EQUAL_INT(humidity, trends.humidity());
  TEST_ASSERT_EQUAL_INT(pressure, trends.pressure());
}

static void test_five_minute_gauge_keeps_trends_after_history_fills() {
  OutdoorTrends trends = makeTrends();
  for (uint32_t minute = 0; minute <= 360; minute += 5) {
    feedFalling(trends, minute);
    if (minute >= 135) assertTrends(trends, -1, -1, -1);
  }
}

static void test_ten_minute_gauge_keeps_trends_after_history_fills() {
  OutdoorTrends trends = makeTrends();
  for (uint32_t minute = 0; minute <= 360; minute += 10) {
    feedFalling(trends, minute);
    if (minute >= 140) assertTrends(trends, -1, -1, -1);
  }
}

static void test_trends_stay_steady_until_enough_history() {
  OutdoorTrends trends = makeTrends();
  for (uint32_t minute = 0; minute <= 135; minute += 5) {
    trends.update(START + minute * MINUTE, true, 0.1f * minute, 0.2f * minute, 900.0f + 0.1f * minute);
    TEST_ASSERT_EQUAL_INT(minute >= 25 ? 1 : 0, trends.temperature());
    TEST_ASSERT_EQUAL_INT(minute >= 25 ? 1 : 0, trends.humidity());
    TEST_ASSERT_EQUAL_INT(minute >= 135 ? 1 : 0, trends.pressure());
  }
}

static void test_small_fluctuations_stay_steady() {
  OutdoorTrends trends = makeTrends();
  for (uint32_t i = 0; i <= 72; i++) {
    const float sign = (i % 2) ? 1.0f : -1.0f;
    trends.update(START + i * 5 * MINUTE, true, 10.0f + 0.1f * sign, 50.0f + 0.9f * sign, 1015.0f + 0.4f * sign);
    assertTrends(trends, 0, 0, 0);
  }
}

static void test_pressure_threshold_applies_over_three_hours() {
  const float ratesPerThreeHours[] = {0.8f, -0.8f, 1.2f, -1.2f};
  const int expected[]             = {0, 0, 1, -1};
  for (int c = 0; c < 4; c++) {
    OutdoorTrends trends = makeTrends();
    for (uint32_t minute = 0; minute <= 360; minute += 5) {
      trends.update(START + minute * MINUTE, true, 10.0f, 50.0f,
                    1000.0f + ratesPerThreeHours[c] * minute / 180.0f);
    }
    TEST_ASSERT_EQUAL_INT(expected[c], trends.pressure());
  }
}

static void test_repeated_fetches_match_single_fetches() {
  OutdoorTrends single   = makeTrends();
  OutdoorTrends repeated = makeTrends();
  for (uint32_t minute = 0; minute <= 360; minute += 5) {
    const uint32_t epoch = START + minute * MINUTE;
    const float t = 10.0f - 0.02f * minute;
    const float h = 90.0f - 0.1f * minute;
    const float p = 1020.0f - 0.02f * minute;
    single.update(epoch, true, t, h, p);
    repeated.update(epoch, true, t, h, p);
    repeated.update(epoch, false, t, h, p);
    repeated.update(epoch, false, t, h, p);
    assertTrends(repeated, single.temperature(), single.humidity(), single.pressure());
  }
}

static void test_repeated_observation_alone_creates_no_trend() {
  OutdoorTrends trends = makeTrends();
  trends.update(START, true, 10.0f, 50.0f, 1000.0f);
  for (int i = 0; i < 200; i++) {
    trends.update(START, false, 10.0f, 50.0f, 1000.0f);
    assertTrends(trends, 0, 0, 0);
  }
}

static void test_long_outage_clears_stale_trends_and_ignores_the_jump() {
  OutdoorTrends trends = makeTrends();
  uint32_t minute = 0;
  for (; minute <= 360; minute += 5) feedFalling(trends, minute);
  assertTrends(trends, -1, -1, -1);

  // Back after six hours with very different readings: no valid reference exists.
  minute += 360;
  trends.update(START + minute * MINUTE, true, 25.0f, 20.0f, 1040.0f);
  assertTrends(trends, 0, 0, 0);

  // Readings stay flat while history rebuilds, so no direction may appear.
  for (uint32_t elapsed = 5; elapsed <= 240; elapsed += 5) {
    trends.update(START + (minute + elapsed) * MINUTE, true, 25.0f, 20.0f, 1040.0f);
    assertTrends(trends, 0, 0, 0);
  }
}

static void test_one_hour_outage_only_clears_the_short_window() {
  OutdoorTrends trends = makeTrends();
  uint32_t minute = 0;
  for (; minute <= 360; minute += 5) feedFalling(trends, minute);

  minute += 55;  // one hour since the previous observation
  feedFalling(trends, minute);
  assertTrends(trends, 0, 0, -1);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_five_minute_gauge_keeps_trends_after_history_fills);
  RUN_TEST(test_ten_minute_gauge_keeps_trends_after_history_fills);
  RUN_TEST(test_trends_stay_steady_until_enough_history);
  RUN_TEST(test_small_fluctuations_stay_steady);
  RUN_TEST(test_pressure_threshold_applies_over_three_hours);
  RUN_TEST(test_repeated_fetches_match_single_fetches);
  RUN_TEST(test_repeated_observation_alone_creates_no_trend);
  RUN_TEST(test_long_outage_clears_stale_trends_and_ignores_the_jump);
  RUN_TEST(test_one_hour_outage_only_clears_the_short_window);
  return UNITY_END();
}
