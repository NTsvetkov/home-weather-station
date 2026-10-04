#pragma once

#include <Arduino.h>

/**
 * @file display.h
 * @brief TFT display initialization and screen rendering.
 */

/** @brief WiFi state as shown in the status bar. */
enum WifiUiState : uint8_t {
  WIFI_UI_OK,          ///< connected; bars from RSSI
  WIFI_UI_CONNECTING,  ///< not connected yet / short outage
  WIFI_UI_DOWN         ///< no credentials or long outage
};

/**
 * @brief Everything the status bar (and freshness logic) needs.
 *
 * Filled by main.cpp before every redraw.
 */
struct UiStatus {
  WifiUiState wifi = WIFI_UI_CONNECTING;
  int32_t rssi     = 0;       ///< dBm, valid when wifi == WIFI_UI_OK

  bool clockValid  = false;   ///< true when local time is known
  uint8_t hour     = 0;       ///< current local hour (0..23), valid when clockValid

  bool haveUpdate  = false;   ///< true when there was at least one successful update
  bool updTimeValid = false;  ///< true when the local time of the update is known
  uint8_t updHour  = 0;       ///< local time of the last successful update
  uint8_t updMin   = 0;
  uint32_t updAgeS = 0;       ///< seconds since the last successful update
};

/** @brief Initialize the TFT display and any related state. */
void initDisplay();

/** @brief Render the main screen (indoor/outdoor readings). */
void drawMainScreen(const UiStatus& st);

/** @brief Render the forecast screen (next days). */
void drawForecastScreen(const UiStatus& st);

/** @brief Render today's 6-hour block forecast screen (4 columns). */
void drawTodayScreen(const UiStatus& st);
