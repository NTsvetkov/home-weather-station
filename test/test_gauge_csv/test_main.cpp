#include <unity.h>

#include "gauge_csv.h"

static const time_t NOW = 1794161662;

void setUp() {}
void tearDown() {}

static const GaugeReading GOOD = {12.5f, 1010.0f, 40.0f, 1794160000};

static void assertUnchanged(const GaugeReading& reading) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, GOOD.temperature, reading.temperature);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, GOOD.pressure, reading.pressure);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, GOOD.humidity, reading.humidity);
  TEST_ASSERT_TRUE(GOOD.observed == reading.observed);
}

static void assertAccepted(const char* payload) {
  GaugeReading reading = GOOD;
  TEST_ASSERT_TRUE_MESSAGE(parseGaugeCsv(payload, NOW, reading), payload);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, reading.temperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1034.2f, reading.pressure);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 73.0f, reading.humidity);
  TEST_ASSERT_TRUE(reading.observed == (time_t)1794161162);
}

static void test_real_gauge_line_uses_sea_level_pressure() {
  assertAccepted("1794161162,6.0,1.5,1008.8,1034.2,73,25,,25\n");
}

static void test_line_endings_are_accepted() {
  assertAccepted("1794161162,6.0,1.5,1008.8,1034.2,73,25,,25");
  assertAccepted("1794161162,6.0,1.5,1008.8,1034.2,73,25,,25\r\n");
}

static void test_timestamp_may_be_up_to_five_minutes_ahead() {
  GaugeReading reading = GOOD;
  TEST_ASSERT_TRUE(parseGaugeCsv("1794161962,6.0,1.5,1008.8,1034.2,73", NOW, reading));
  reading = GOOD;
  TEST_ASSERT_FALSE(parseGaugeCsv("1794161963,6.0,1.5,1008.8,1034.2,73", NOW, reading));
  assertUnchanged(reading);
}

static void test_invalid_payloads_are_rejected_and_keep_good_data() {
  const char* const invalid[] = {
      "",
      "\n",
      "not,a,gauge,response",
      "<html>Service unavailable</html>",
      "1794161162,6.0,1.5",                            // truncated
      "1794161162,6.0,1.5,1008.8,1034.2",              // missing humidity
      "1794161162,,1.5,1008.8,1034.2,73",              // empty temperature
      "1794161162,6.0,1.5,1008.8,,73",                 // empty pressure
      "1794161162,6.0,1.5,1008.8,1034.2,",             // empty humidity
      "1794161162,nan,1.5,1008.8,1034.2,73",           // not a number
      "1794161162,6.0,1.5,1008.8,nan,73",
      "1794161162,6.0,1.5,1008.8,1034.2,nan",
      "1794161162,inf,1.5,1008.8,1034.2,73",           // infinite
      "1794161162,6.0,1.5,1008.8,-inf,73",
      "1794161162,1e99,1.5,1008.8,1034.2,73",          // overflows float
      "1794161162,6.0x,1.5,1008.8,1034.2,73",          // trailing text
      "1794161162,6.0,1.5,1008.8,1034.2,73%",
      "1794161162,70.0,1.5,1008.8,1034.2,73",          // temperature out of range
      "1794161162,6.0,1.5,1008.8,700.0,73",            // pressure out of range
      "1794161162,6.0,1.5,1008.8,1034.2,101",          // humidity out of range
      "179416116,6.0,1.5,1008.8,1034.2,73",            // short timestamp
      "17941611620,6.0,1.5,1008.8,1034.2,73",          // long timestamp
      "179416116x,6.0,1.5,1008.8,1034.2,73",           // timestamp with text
      "1500000000,6.0,1.5,1008.8,1034.2,73",           // before 2021
      "1794169999,6.0,1.5,1008.8,1034.2,73",           // far in the future
      "1794161162,123456789012345678901234567890123,1.5,1008.8,1034.2,73",  // oversized field
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    GaugeReading reading = GOOD;
    TEST_ASSERT_FALSE_MESSAGE(parseGaugeCsv(invalid[i], NOW, reading), invalid[i]);
    assertUnchanged(reading);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_real_gauge_line_uses_sea_level_pressure);
  RUN_TEST(test_line_endings_are_accepted);
  RUN_TEST(test_timestamp_may_be_up_to_five_minutes_ahead);
  RUN_TEST(test_invalid_payloads_are_rejected_and_keep_good_data);
  return UNITY_END();
}
