#include <string.h>
#include <unity.h>

#include "reset_diary.h"

static const uint32_t NOW    = 1794161162UL;
static const uint32_t DAY    = 86400UL;
static const uint32_t EXPIRY = 7 * DAY;

static const ResetCause CRASH    = {RESET_EXCEPTION, 28, 0x40201234UL, 0x00000010UL};
static const ResetCause WATCHDOG = {RESET_SW_WDT, 0, 0, 0};
static const ResetCause POWER    = {RESET_POWER_ON, 0, 0, 0};
static const ResetCause EXTERNAL = {RESET_EXTERNAL, 0, 0, 0};

void setUp() {}
void tearDown() {}

static ResetPending coldBoot() {
  ResetPending pending;
  memset(&pending, 0xA5, sizeof(pending));  // RTC memory after power loss
  resetPendingOnBoot(pending, POWER);
  return pending;
}

static void test_only_faults_count_as_unexpected() {
  TEST_ASSERT_FALSE(isUnexpectedReset(RESET_POWER_ON));
  TEST_ASSERT_TRUE(isUnexpectedReset(RESET_HW_WDT));
  TEST_ASSERT_TRUE(isUnexpectedReset(RESET_EXCEPTION));
  TEST_ASSERT_TRUE(isUnexpectedReset(RESET_SW_WDT));
  TEST_ASSERT_TRUE(isUnexpectedReset(RESET_SOFT_RESTART));
  TEST_ASSERT_FALSE(isUnexpectedReset(RESET_DEEP_SLEEP));
  TEST_ASSERT_FALSE(isUnexpectedReset(RESET_EXTERNAL));
}

static void test_cold_boot_discards_rtc_garbage() {
  const ResetPending pending = coldBoot();
  TEST_ASSERT_TRUE(resetIsValid(pending));
  TEST_ASSERT_EQUAL_UINT32(0, pending.count);
  TEST_ASSERT_EQUAL_UINT32(RESET_NO_BLOCK, pending.lowestNow);
}

static void test_crash_keeps_cause_and_lowest_block() {
  ResetPending pending = coldBoot();
  resetPendingNoteBlock(pending, 23000);
  resetPendingOnBoot(pending, CRASH);
  TEST_ASSERT_EQUAL_UINT32(1, pending.count);
  TEST_ASSERT_EQUAL_UINT32(RESET_EXCEPTION, pending.last.reason);
  TEST_ASSERT_EQUAL_UINT32(0x40201234UL, pending.last.epc1);
  TEST_ASSERT_EQUAL_UINT32(23000, pending.lowestBeforeLast);
  TEST_ASSERT_EQUAL_UINT32(RESET_NO_BLOCK, pending.lowestNow);
}

static void test_upload_or_rst_button_is_not_counted() {
  ResetPending pending = coldBoot();
  resetPendingNoteBlock(pending, 23000);
  resetPendingOnBoot(pending, EXTERNAL);
  TEST_ASSERT_EQUAL_UINT32(0, pending.count);
  TEST_ASSERT_EQUAL_UINT32(RESET_NO_BLOCK, pending.lowestNow);
}

static void test_lowest_block_only_moves_down() {
  ResetPending pending = coldBoot();
  TEST_ASSERT_TRUE(resetPendingNoteBlock(pending, 23000));
  TEST_ASSERT_FALSE(resetPendingNoteBlock(pending, 30000));
  TEST_ASSERT_FALSE(resetPendingNoteBlock(pending, 23000));
  TEST_ASSERT_TRUE(resetPendingNoteBlock(pending, 15000));
  TEST_ASSERT_EQUAL_UINT32(15000, pending.lowestNow);
  TEST_ASSERT_TRUE(resetIsValid(pending));
}

static void test_crash_loop_needs_a_single_flash_write() {
  ResetPending pending = coldBoot();
  ResetDiary diary{};
  for (int i = 0; i < 50; i++) resetPendingOnBoot(pending, CRASH);

  TEST_ASSERT_TRUE(resetDiaryCommit(diary, pending, NOW, EXPIRY));
  TEST_ASSERT_EQUAL_UINT32(50, diary.count);
  TEST_ASSERT_EQUAL_UINT32(0, pending.count);
  TEST_ASSERT_FALSE(resetDiaryCommit(diary, pending, NOW, EXPIRY));
}

static void test_commit_ignores_a_clean_boot() {
  ResetPending pending = coldBoot();
  ResetDiary diary{};
  TEST_ASSERT_FALSE(resetDiaryCommit(diary, pending, NOW, EXPIRY));
  TEST_ASSERT_FALSE(resetDiaryVisible(diary, NOW, EXPIRY));
}

static void test_blank_flash_is_treated_as_empty() {
  ResetPending pending = coldBoot();
  ResetDiary diary;
  memset(&diary, 0xFF, sizeof(diary));
  TEST_ASSERT_FALSE(resetDiaryVisible(diary, NOW, EXPIRY));

  resetPendingOnBoot(pending, WATCHDOG);
  TEST_ASSERT_TRUE(resetDiaryCommit(diary, pending, NOW, EXPIRY));
  TEST_ASSERT_EQUAL_UINT32(1, diary.count);
  TEST_ASSERT_EQUAL_UINT32(RESET_SW_WDT, diary.last.reason);
}

static void test_corrupted_diary_is_not_shown() {
  ResetPending pending = coldBoot();
  ResetDiary diary{};
  resetPendingOnBoot(pending, CRASH);
  resetDiaryCommit(diary, pending, NOW, EXPIRY);
  diary.count = 99;
  TEST_ASSERT_FALSE(resetDiaryVisible(diary, NOW, EXPIRY));
}

static void test_indicator_expires_and_count_restarts() {
  ResetPending pending = coldBoot();
  ResetDiary diary{};
  resetPendingOnBoot(pending, CRASH);
  resetDiaryCommit(diary, pending, NOW, EXPIRY);

  TEST_ASSERT_TRUE(resetDiaryVisible(diary, NOW + EXPIRY, EXPIRY));
  TEST_ASSERT_FALSE(resetDiaryVisible(diary, NOW + EXPIRY + 1, EXPIRY));

  resetPendingOnBoot(pending, WATCHDOG);
  resetDiaryCommit(diary, pending, NOW + 30 * DAY, EXPIRY);
  TEST_ASSERT_EQUAL_UINT32(1, diary.count);
  TEST_ASSERT_EQUAL_UINT32(RESET_SW_WDT, diary.last.reason);
}

static void test_counts_add_up_within_the_expiry_period() {
  ResetPending pending = coldBoot();
  ResetDiary diary{};
  resetPendingOnBoot(pending, CRASH);
  resetDiaryCommit(diary, pending, NOW, EXPIRY);
  resetPendingOnBoot(pending, WATCHDOG);
  resetDiaryCommit(diary, pending, NOW + DAY, EXPIRY);
  TEST_ASSERT_EQUAL_UINT32(2, diary.count);
  TEST_ASSERT_EQUAL_UINT32(RESET_SW_WDT, diary.last.reason);
}

static void test_commit_without_clock_stays_visible() {
  ResetPending pending = coldBoot();
  ResetDiary diary{};
  resetPendingOnBoot(pending, CRASH);
  TEST_ASSERT_TRUE(resetDiaryCommit(diary, pending, 0, EXPIRY));
  TEST_ASSERT_EQUAL_UINT32(0, diary.epoch);
  TEST_ASSERT_TRUE(resetDiaryVisible(diary, NOW + 365 * DAY, EXPIRY));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_only_faults_count_as_unexpected);
  RUN_TEST(test_cold_boot_discards_rtc_garbage);
  RUN_TEST(test_crash_keeps_cause_and_lowest_block);
  RUN_TEST(test_upload_or_rst_button_is_not_counted);
  RUN_TEST(test_lowest_block_only_moves_down);
  RUN_TEST(test_crash_loop_needs_a_single_flash_write);
  RUN_TEST(test_commit_ignores_a_clean_boot);
  RUN_TEST(test_blank_flash_is_treated_as_empty);
  RUN_TEST(test_corrupted_diary_is_not_shown);
  RUN_TEST(test_indicator_expires_and_count_restarts);
  RUN_TEST(test_counts_add_up_within_the_expiry_period);
  RUN_TEST(test_commit_without_clock_stays_visible);
  return UNITY_END();
}
