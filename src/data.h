#pragma once
#include <Arduino.h>

/**
 * @brief One day of forecast data (already post-processed for the UI).
 */
struct ForecastDay {
  char label[11];    // YYYY-MM-DD + NUL
  int wmoCode;       // WMO weather code (used only as a hint, e.g. thunderstorms)
  float tMin;
  float tMax;
  float precip;      // mm
  float windMax;     // km/h
  float cloudMean;   // %
  bool valid;
};

/** @brief Outdoor temperature (C). Valid only when haveExtData == true. */
extern float extTemperature;
/** @brief Outdoor humidity (%). Valid only when haveExtData == true. */
extern float extHumidity;
/** @brief Outdoor pressure (hPa). Valid only when haveExtData == true. */
extern float extPressure;
/** @brief Indoor temperature (C). Valid only when haveIntData == true. */
extern float intTemperature;
/** @brief Indoor humidity (%). Valid only when haveIntData == true. */
extern float intHumidity;
/** @brief True when the latest gauge read succeeded and ext* values are valid. */
extern bool haveExtData;
/** @brief True when the latest internal sensor read succeeded and int* values are valid. */
extern bool haveIntData;

/**
 * @brief Trend for outdoor temperature.
 * -1 = falling, 0 = steady/unknown, +1 = rising
 */
extern int8_t extTempTrend;
/**
 * @brief Trend for outdoor humidity.
 * -1 = falling, 0 = steady/unknown, +1 = rising
 */
extern int8_t extHumTrend;
/**
 * @brief Trend for outdoor pressure.
 * -1 = falling, 0 = steady/unknown, +1 = rising
 */
extern int8_t extPressTrend;

/** @brief Array with up to 3 upcoming forecast days (tomorrow + next 2). */
extern ForecastDay forecast[3];
/** @brief Number of valid entries in forecast[]. */
extern int forecastCount;

/**
 * @brief One 6-hour block of today's forecast (aggregated from hourly data).
 */
struct ForecastBlock {
  float tMin;        ///< minimum temperature in block (C)
  float tMax;        ///< maximum temperature in block (C)
  float precip;      ///< total precipitation in block (mm)
  float windMax;     ///< maximum wind speed in block (km/h)
  float cloudMean;   ///< mean cloud cover in block (%)
  int wmoCode;       ///< most severe WMO weather code in block
  bool valid;
};

/** @brief Today's forecast split into 4 blocks: 00-06, 06-12, 12-18, 18-24. */
extern ForecastBlock todayBlocks[4];
/** @brief True when todayBlocks[] contain valid data. */
extern bool haveTodayForecast;

/**
 * @brief Fetch outdoor data from meter.ac gauge endpoint.
 * @return true if parsing succeeded and ext* values were updated.
 */
bool fetchGaugeData();

/**
 * @brief Fetch forecast from Open-Meteo and fill forecast[] for the next days.
 * @return true if at least one forecast day is available.
 */
bool fetchForecast();

/**
 * @brief Fetch today's hourly forecast and aggregate into 6-hour blocks.
 * @return true if at least one block was populated.
 */
bool fetchHourlyForecast();
