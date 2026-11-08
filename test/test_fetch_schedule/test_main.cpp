#include <unity.h>

#include "fetch_schedule.h"

// Firmware defaults.
static const uint32_t GAUGE_NORMAL    = 180000UL;
static const uint32_t GAUGE_RETRY     = 30000UL;
static const uint32_t STARTUP_RETRY   = 2000UL;
static const uint32_t FORECAST_NORMAL = 3600000UL;
static const uint32_t FORECAST_RETRY  = 300000UL;

void setUp() {}
void tearDown() {}

static void test_first_request_is_due_immediately() {
  FetchSchedule schedule;
  TEST_ASSERT_TRUE(schedule.due(0, GAUGE_NORMAL, GAUGE_RETRY));
}

static void test_success_waits_for_normal_interval() {
  FetchSchedule schedule;
  schedule.finished(true, 5000);
  TEST_ASSERT_TRUE(schedule.haveSuccess);
  TEST_ASSERT_FALSE(schedule.due(5000 + GAUGE_NORMAL - 1, GAUGE_NORMAL, GAUGE_RETRY));
  TEST_ASSERT_TRUE(schedule.due(5000 + GAUGE_NORMAL, GAUGE_NORMAL, GAUGE_RETRY));
}

static void test_failed_startup_request_respects_pause_from_completion() {
  FetchSchedule schedule;
  // The request started at 1000 and took four seconds to fail.
  schedule.finished(false, 5000);
  TEST_ASSERT_FALSE(schedule.haveSuccess);
  TEST_ASSERT_FALSE(schedule.due(5000 + STARTUP_RETRY - 1, GAUGE_NORMAL, STARTUP_RETRY));
  TEST_ASSERT_TRUE(schedule.due(5000 + STARTUP_RETRY, GAUGE_NORMAL, STARTUP_RETRY));
}

static void test_failed_midnight_refresh_respects_pause() {
  FetchSchedule schedule;
  schedule.finished(true, 1000);

  schedule.refresh();
  const uint32_t midnight = 2000;
  TEST_ASSERT_TRUE(schedule.due(midnight, FORECAST_NORMAL, FORECAST_RETRY));

  schedule.finished(false, midnight);
  TEST_ASSERT_FALSE(schedule.due(midnight + 1, FORECAST_NORMAL, FORECAST_RETRY));
  TEST_ASSERT_FALSE(schedule.due(midnight + FORECAST_RETRY - 1, FORECAST_NORMAL, FORECAST_RETRY));
  TEST_ASSERT_TRUE(schedule.due(midnight + FORECAST_RETRY, FORECAST_NORMAL, FORECAST_RETRY));
}

static void test_refresh_keeps_previous_success() {
  FetchSchedule schedule;
  schedule.finished(true, 1000);
  schedule.refresh();
  TEST_ASSERT_TRUE(schedule.haveSuccess);
  TEST_ASSERT_EQUAL_UINT32(1000, schedule.lastSuccessMs);
}

static void test_failure_keeps_last_success_time() {
  FetchSchedule schedule;
  schedule.finished(true, 1000);
  schedule.finished(false, 9000);
  TEST_ASSERT_TRUE(schedule.haveSuccess);
  TEST_ASSERT_EQUAL_UINT32(1000, schedule.lastSuccessMs);
  TEST_ASSERT_EQUAL_UINT32(9000, schedule.lastAttemptMs);
}

static void test_backoff_doubles_and_stops_growing() {
  FetchSchedule schedule;
  const uint32_t expected[] = {2000, 4000, 8000, 16000, 32000, 32000, 32000};
  uint32_t now = 0;
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
    schedule.finished(false, now);
    TEST_ASSERT_FALSE(schedule.due(now + expected[i] - 1, GAUGE_NORMAL, STARTUP_RETRY));
    TEST_ASSERT_TRUE(schedule.due(now + expected[i], GAUGE_NORMAL, STARTUP_RETRY));
    now += expected[i];
  }
}

static void test_backoff_never_exceeds_normal_interval() {
  FetchSchedule schedule;
  for (int i = 0; i < 300; i++) schedule.finished(false, 0);
  TEST_ASSERT_EQUAL_UINT8(255, schedule.failures);
  TEST_ASSERT_FALSE(schedule.due(GAUGE_NORMAL - 1, GAUGE_NORMAL, GAUGE_RETRY));
  TEST_ASSERT_TRUE(schedule.due(GAUGE_NORMAL, GAUGE_NORMAL, GAUGE_RETRY));
}

static void test_success_clears_backoff() {
  FetchSchedule schedule;
  schedule.finished(false, 0);
  schedule.finished(false, 0);
  schedule.finished(true, 1000);
  TEST_ASSERT_EQUAL_UINT8(0, schedule.failures);
  TEST_ASSERT_FALSE(schedule.due(1000 + GAUGE_RETRY, GAUGE_NORMAL, GAUGE_RETRY));
}

static void test_timing_survives_millis_rollover() {
  FetchSchedule schedule;
  const uint32_t beforeRollover = 0xFFFFFF00UL;
  schedule.finished(false, beforeRollover);
  TEST_ASSERT_FALSE(schedule.due(beforeRollover + GAUGE_RETRY - 1, GAUGE_NORMAL, GAUGE_RETRY));
  TEST_ASSERT_TRUE(schedule.due(beforeRollover + GAUGE_RETRY, GAUGE_NORMAL, GAUGE_RETRY));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_request_is_due_immediately);
  RUN_TEST(test_success_waits_for_normal_interval);
  RUN_TEST(test_failed_startup_request_respects_pause_from_completion);
  RUN_TEST(test_failed_midnight_refresh_respects_pause);
  RUN_TEST(test_refresh_keeps_previous_success);
  RUN_TEST(test_failure_keeps_last_success_time);
  RUN_TEST(test_backoff_doubles_and_stops_growing);
  RUN_TEST(test_backoff_never_exceeds_normal_interval);
  RUN_TEST(test_success_clears_backoff);
  RUN_TEST(test_timing_survives_millis_rollover);
  return UNITY_END();
}
