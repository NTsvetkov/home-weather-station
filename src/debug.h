/**
 * @file debug.h
 * @brief Unified debug logging macros.
 *
 * Provides LOG_I (info), LOG_W (warning), LOG_E (error) macros that:
 * - Store format strings in PROGMEM (F() macro) to save RAM.
 * - Can be globally disabled by defining DEBUG_DISABLE before including this header.
 * - Print a consistent prefix for each log level.
 *
 * Usage:
 *   LOG_I("WiFi connected");
 *   LOG_W("Retrying in %u ms", interval);
 *   LOG_E("HTTP error: %d", code);
 */
#pragma once

#include <Arduino.h>

// Set default log level if not defined.
// 0 = off, 1 = errors only, 2 = errors + warnings, 3 = all (info)
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 3
#endif

#if DEBUG_LEVEL >= 1
  #define LOG_E(fmt, ...) Serial.printf_P(PSTR("[E] " fmt "\n"), ##__VA_ARGS__)
#else
  #define LOG_E(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 2
  #define LOG_W(fmt, ...) Serial.printf_P(PSTR("[W] " fmt "\n"), ##__VA_ARGS__)
#else
  #define LOG_W(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 3
  #define LOG_I(fmt, ...) Serial.printf_P(PSTR("[I] " fmt "\n"), ##__VA_ARGS__)
#else
  #define LOG_I(fmt, ...) ((void)0)
#endif

