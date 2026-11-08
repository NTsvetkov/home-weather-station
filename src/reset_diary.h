#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file reset_diary.h
 * @brief Bookkeeping for unexpected resets, free of Arduino dependencies.
 *
 * ResetPending lives in RTC memory, which survives crashes but not power loss.
 * ResetDiary lives in flash and is written once per stable boot, so a crash
 * loop only increments RTC counters and cannot wear out the flash.
 */

static constexpr uint32_t RESET_NO_BLOCK = UINT32_MAX;
static constexpr uint32_t RESET_MAGIC    = 0x52445931UL;

// Values match the ESP8266 SDK's rst_reason.
enum ResetReason : uint32_t {
  RESET_POWER_ON     = 0,
  RESET_HW_WDT       = 1,
  RESET_EXCEPTION    = 2,
  RESET_SW_WDT       = 3,
  RESET_SOFT_RESTART = 4,
  RESET_DEEP_SLEEP   = 5,
  RESET_EXTERNAL     = 6,
};

struct ResetCause {
  uint32_t reason;
  uint32_t exccause;
  uint32_t epc1;
  uint32_t excvaddr;
};

/** @brief Counters kept across warm resets. */
struct ResetPending {
  uint32_t magic;
  uint32_t count;
  ResetCause last;
  uint32_t lowestBeforeLast;  ///< lowest largest-block value before the last unexpected reset
  uint32_t lowestNow;         ///< lowest largest-block value since this boot
  uint32_t checksum;
};

/** @brief Record kept in flash across power loss. */
struct ResetDiary {
  uint32_t magic;
  uint32_t count;
  ResetCause last;
  uint32_t lowestBeforeLast;
  uint32_t epoch;  ///< time of the last write, 0 if the clock was not set
  uint32_t checksum;
};

/** @brief Watchdogs, crashes and software restarts; not power loss, RST or uploads. */
inline bool isUnexpectedReset(uint32_t reason) {
  return reason >= RESET_HW_WDT && reason <= RESET_SOFT_RESTART;
}

inline const char* resetLabel(uint32_t reason) {
  switch (reason) {
    case RESET_HW_WDT:       return "HW WDT";
    case RESET_EXCEPTION:    return "CRASH";
    case RESET_SW_WDT:       return "SW WDT";
    case RESET_SOFT_RESTART: return "RESTART";
    default:                 return "?";
  }
}

template <typename T>
uint32_t resetChecksum(const T& record) {
  uint32_t hash = 2166136261UL;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  for (size_t i = 0; i < offsetof(T, checksum); i++) {
    hash = (hash ^ bytes[i]) * 16777619UL;
  }
  return hash;
}

template <typename T>
void resetSeal(T& record) {
  record.magic    = RESET_MAGIC;
  record.checksum = resetChecksum(record);
}

template <typename T>
bool resetIsValid(const T& record) {
  return record.magic == RESET_MAGIC && record.checksum == resetChecksum(record);
}

/** @brief Update the RTC counters at boot; RTC contents are garbage after power loss. */
inline void resetPendingOnBoot(ResetPending& pending, const ResetCause& cause) {
  if (!resetIsValid(pending)) {
    pending = ResetPending{};
    pending.lowestNow = pending.lowestBeforeLast = RESET_NO_BLOCK;
  }
  if (isUnexpectedReset(cause.reason)) {
    pending.count++;
    pending.last = cause;
    pending.lowestBeforeLast = pending.lowestNow;
  }
  pending.lowestNow = RESET_NO_BLOCK;
  resetSeal(pending);
}

/** @return true if the value is a new low and the record changed. */
inline bool resetPendingNoteBlock(ResetPending& pending, uint32_t block) {
  if (block >= pending.lowestNow) return false;
  pending.lowestNow = block;
  resetSeal(pending);
  return true;
}

inline bool resetDiaryExpired(const ResetDiary& diary, uint32_t now, uint32_t expiryS) {
  return diary.epoch != 0 && now > diary.epoch && now - diary.epoch > expiryS;
}

/**
 * @brief Move pending resets into the diary.
 * @param now Current Unix time, or 0 if the clock is not set.
 * @return true if the diary changed and must be written to flash.
 */
inline bool resetDiaryCommit(ResetDiary& diary, ResetPending& pending, uint32_t now, uint32_t expiryS) {
  if (pending.count == 0) return false;
  if (!resetIsValid(diary) || resetDiaryExpired(diary, now, expiryS)) diary = ResetDiary{};
  diary.count += pending.count;
  diary.last = pending.last;
  diary.lowestBeforeLast = pending.lowestBeforeLast;
  diary.epoch = now;
  resetSeal(diary);
  pending.count = 0;
  resetSeal(pending);
  return true;
}

inline bool resetDiaryVisible(const ResetDiary& diary, uint32_t now, uint32_t expiryS) {
  return resetIsValid(diary) && diary.count > 0 && !resetDiaryExpired(diary, now, expiryS);
}
