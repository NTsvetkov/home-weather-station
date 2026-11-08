#include <Arduino.h>
#include <EEPROM.h>
#include <time.h>
#include <user_interface.h>

#include "project_config.h"
#include "debug.h"
#include "reset_diary.h"
#include "reset_log.h"

/**
 * @file reset_log.cpp
 * @brief Stores unexpected resets in RTC memory and flash.
 */

// How long the status bar keeps showing the last unexpected reset.
#ifndef CFG_RESET_INDICATOR_DAYS
#define CFG_RESET_INDICATOR_DAYS 7
#endif
// Warn when the largest free block drops close to the 16 KB TLS buffer.
#ifndef CFG_LOW_BLOCK_WARN_BYTES
#define CFG_LOW_BLOCK_WARN_BYTES 18432UL
#endif

static const uint32_t EXPIRY_S           = (uint32_t)CFG_RESET_INDICATOR_DAYS * 86400UL;
static const uint32_t WRITE_WITHOUT_NTP_MS = 300000UL;
static const time_t MIN_VALID_EPOCH      = 1609459200;

// The first 32 blocks (128 bytes) of RTC user memory belong to the eboot bootloader.
static const uint32_t RTC_OFFSET_BLOCKS  = 32;

static ResetPending pending;
static ResetDiary diary;

static void savePending() {
  ESP.rtcUserMemoryWrite(RTC_OFFSET_BLOCKS, reinterpret_cast<uint32_t*>(&pending), sizeof(pending));
}

/** @return Current Unix time, or 0 if the clock is not set. */
static uint32_t currentEpoch() {
  const time_t now = time(nullptr);
  return now >= MIN_VALID_EPOCH ? (uint32_t)now : 0;
}

void resetLogBegin() {
  const rst_info* info = ESP.getResetInfoPtr();
  const ResetCause cause = {info->reason, info->exccause, info->epc1, info->excvaddr};

  ESP.rtcUserMemoryRead(RTC_OFFSET_BLOCKS, reinterpret_cast<uint32_t*>(&pending), sizeof(pending));
  resetPendingOnBoot(pending, cause);
  savePending();

  EEPROM.begin(sizeof(diary));
  EEPROM.get(0, diary);
  EEPROM.end();

  if (isUnexpectedReset(cause.reason)) {
    LOG_W("Unexpected reset: %s, exccause %lu, epc1 0x%08lx, addr 0x%08lx, lowest block %lu",
          resetLabel(cause.reason), (unsigned long)cause.exccause, (unsigned long)cause.epc1,
          (unsigned long)cause.excvaddr, (unsigned long)pending.lowestBeforeLast);
  }
  if (resetIsValid(diary) && diary.count > 0) {
    LOG_W("Reset diary: %lu unexpected, last %s at %lu, exccause %lu, epc1 0x%08lx, addr 0x%08lx",
          (unsigned long)diary.count, resetLabel(diary.last.reason), (unsigned long)diary.epoch,
          (unsigned long)diary.last.exccause, (unsigned long)diary.last.epc1,
          (unsigned long)diary.last.excvaddr);
  }
}

void resetLogLoop() {
  if (pending.count == 0) return;
  const uint32_t now = currentEpoch();
  if (now == 0 && millis() < WRITE_WITHOUT_NTP_MS) return;

  if (resetDiaryCommit(diary, pending, now, EXPIRY_S)) {
    EEPROM.begin(sizeof(diary));
    EEPROM.put(0, diary);
    EEPROM.commit();
    EEPROM.end();
    savePending();
    LOG_I("Reset diary saved: %lu unexpected", (unsigned long)diary.count);
  }
}

void resetLogNoteBlock(uint32_t block) {
  if (resetPendingNoteBlock(pending, block)) savePending();
}

bool resetLogIndicator(char* line1, size_t line1Size, char* line2, size_t line2Size) {
  if (resetDiaryVisible(diary, currentEpoch(), EXPIRY_S)) {
    snprintf(line1, line1Size, "R%lu %s", (unsigned long)diary.count, resetLabel(diary.last.reason));

    char date[8] = "--.--";
    if (diary.epoch != 0) {
      const time_t epoch = (time_t)diary.epoch;
      struct tm ti;
      localtime_r(&epoch, &ti);
      snprintf(date, sizeof(date), "%02u.%02u", (uint8_t)ti.tm_mday, (uint8_t)(ti.tm_mon + 1));
    }
    if (diary.lowestBeforeLast == RESET_NO_BLOCK) {
      snprintf(line2, line2Size, "%s --K", date);
    } else {
      snprintf(line2, line2Size, "%s %luK", date, (unsigned long)(diary.lowestBeforeLast / 1024UL));
    }
    return true;
  }

  if (pending.lowestNow != RESET_NO_BLOCK && pending.lowestNow < CFG_LOW_BLOCK_WARN_BYTES) {
    snprintf(line1, line1Size, "MIN %luK", (unsigned long)(pending.lowestNow / 1024UL));
    line2[0] = '\0';
    return true;
  }
  return false;
}
