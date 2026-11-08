#pragma once

#include <stdint.h>

/**
 * @file fetch_schedule.h
 * @brief Request timing and retry backoff, free of Arduino dependencies.
 *
 * All retries use the completion time, including failed requests at boot/midnight.
 */
struct FetchSchedule {
  bool attempted = false;
  bool haveSuccess = false;
  uint8_t failures = 0;
  uint32_t lastAttemptMs = 0;
  uint32_t lastSuccessMs = 0;

  bool due(uint32_t now, uint32_t normal, uint32_t retry) const {
    uint32_t interval = normal;
    if (failures) {
      const uint8_t shift = failures > 5 ? 4 : failures - 1;
      uint64_t backoff = (uint64_t)retry << shift;
      interval = backoff > normal ? normal : (uint32_t)backoff;
    }
    return !attempted || now - lastAttemptMs >= interval;
  }
  /** @param now millis() read after the request completed. */
  void finished(bool ok, uint32_t now) {
    attempted = true;
    lastAttemptMs = now;
    if (ok) {
      haveSuccess = true;
      lastSuccessMs = now;
      failures = 0;
    } else if (failures < 255) {
      ++failures;
    }
  }
  void refresh() { attempted = false; failures = 0; }
};
