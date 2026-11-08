#pragma once

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @file gauge_csv.h
 * @brief Parser for the meter.ac gauge CSV, free of Arduino dependencies.
 */

/** @brief One validated line from the gauge. */
struct GaugeReading {
  float temperature;  ///< temperature (C)
  float pressure;     ///< pressure (hPa)
  float humidity;     ///< humidity (%)
  time_t observed;    ///< observation time (Unix seconds)
};

inline bool inRange(float value, float low, float high) {
  return isfinite(value) && value >= low && value <= high;
}

/**
 * @brief Parse float from a span (non-null-terminated) substring.
 *
 * Copies the span into a small temporary buffer, then uses strtof().
 */
inline bool parseFloatSpan(const char* start, size_t len, float& out) {
  // Values in gauge.txt are short (e.g. "-12.3").
  if (len == 0 || len >= 32) return false;
  char buf[32];
  memcpy(buf, start, len);
  buf[len] = '\0';

  char* endptr = nullptr;
  errno = 0;
  out          = strtof(buf, &endptr);
  if (endptr == buf || errno == ERANGE || !isfinite(out)) return false;
  while (*endptr == ' ' || *endptr == '\t') ++endptr;
  return *endptr == '\0';
}

/**
 * @brief Parse gauge CSV payload.
 *
 * Mapping:
 * - field0 = timestamp
 * - field1 = temperature
 * - field4 = pressure
 * - field5 = humidity
 *
 * @param now Current Unix time, used to reject observations from the future.
 * @param[out] out Assigned only when the whole line is valid.
 */
inline bool parseGaugeCsv(const char* payload, time_t now, GaugeReading& out) {
  GaugeReading parsed{};
  const char* fieldStart = payload;
  int field              = 0;

  bool gotTemp  = false;
  bool gotPress = false;
  bool gotHum   = false;
  bool gotTime  = false;

  for (const char* p = payload;; p++) {
    char c     = *p;
    bool atEnd = (c == '\0' || c == '\n' || c == '\r');
    if (c == ',' || atEnd) {
      size_t fieldLen = (size_t)(p - fieldStart);
      if (field == 0) {
        if (fieldLen == 10) {
          uint64_t epoch = 0;
          gotTime = true;
          for (size_t i = 0; i < fieldLen; ++i) {
            if (fieldStart[i] < '0' || fieldStart[i] > '9') { gotTime = false; break; }
            epoch = epoch * 10UL + (uint32_t)(fieldStart[i] - '0');
          }
          gotTime = gotTime && epoch >= 1609459200 && epoch <= (uint64_t)now + 300;
          if (gotTime) parsed.observed = (time_t)epoch;
        }
      } else if (field == 1) {
        gotTemp = parseFloatSpan(fieldStart, fieldLen, parsed.temperature);
      } else if (field == 4) {
        gotPress = parseFloatSpan(fieldStart, fieldLen, parsed.pressure);
      } else if (field == 5) {
        gotHum = parseFloatSpan(fieldStart, fieldLen, parsed.humidity);
      }

      if (atEnd) break;
      field++;
      fieldStart = p + 1;
    }
  }

  if (!gotTime || !gotTemp || !gotPress || !gotHum ||
      !inRange(parsed.temperature, -80, 65) || !inRange(parsed.pressure, 800, 1100) ||
      !inRange(parsed.humidity, 0, 100)) {
    return false;
  }
  out = parsed;
  return true;
}
