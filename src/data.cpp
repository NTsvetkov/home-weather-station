#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <cstring>
#include <stdlib.h>

// Local config (gitignored) or example fallback for CI.
#if defined(__has_include)
  #if __has_include("config.h")
    #include "config.h"
  #elif __has_include("config.example.h")
    #include "config.example.h"
  #endif
#else
  #include "config.h"
#endif

#include "data.h"
#include "debug.h"

// Fallback defaults if config constants are missing (CI without config.h).
#ifndef CFG_GAUGE_HTTP_TIMEOUT_MS
  #define CFG_GAUGE_HTTP_TIMEOUT_MS 3500UL
#endif
#ifndef CFG_GAUGE_URL
  #define CFG_GAUGE_URL "https://meter.ac/gs/nodes/N200/gauge.txt"
#endif
#ifndef CFG_TLS_RX_BUFFER_BYTES_FORECAST
  #define CFG_TLS_RX_BUFFER_BYTES_FORECAST 2048
#endif
#ifndef CFG_TLS_TX_BUFFER_BYTES_FORECAST
  #define CFG_TLS_TX_BUFFER_BYTES_FORECAST 512
#endif
#ifndef CFG_FORECAST_URL
  #define CFG_FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=42.1859191&longitude=24.3398302&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,wind_speed_10m_max,cloud_cover_mean&models=ecmwf_ifs&timezone=auto"
#endif
#ifndef CFG_FORECAST_JSON_DOC_CAPACITY
  #define CFG_FORECAST_JSON_DOC_CAPACITY 8000
#endif

// Trend window is configured in config.h (minutes).
// We expect it as a macro (so every translation unit sees the same value).
// If it's missing, keep a safe default.
#ifndef TREND_WINDOW_MINUTES
#define TREND_WINDOW_MINUTES 10
#endif

/**
 * @file data.cpp
 * @brief Networking + parsing for external (gauge/forecast) data.
 */

// External readings
float extTemperature = 0.0f;
float extHumidity    = 0.0f;
float extPressure    = 0.0f;
bool haveExtData     = false;

// Trend indicators
int8_t extTempTrend  = 0;
int8_t extHumTrend   = 0;
int8_t extPressTrend = 0;

bool haveIntData = false;

// Internal readings (filled from sensors module via main)
float intTemperature = 0.0f;
float intHumidity    = 0.0f;

ForecastDay forecast[3];
int forecastCount = 0;

/* -------------------------------------------------------------------------- */
/*                         Outdoor Trend History                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief A single timestamped sample of outdoor readings.
 */
struct ExtSample {
  uint32_t ms;  ///< millis() timestamp
  float t;      ///< temperature (C)
  float h;      ///< humidity (%)
  float p;      ///< pressure (hPa)
};

static ExtSample extHist[8];
static uint8_t extHistCount = 0;
static uint8_t extHistHead  = 0;

/**
 * @brief Push a new outdoor sample into the ring buffer.
 */
static void pushExtSample(float t, float h, float p) {
  extHist[extHistHead] = { millis(), t, h, p };
  extHistHead = (extHistHead + 1) % (uint8_t)(sizeof(extHist) / sizeof(extHist[0]));
  if (extHistCount < (uint8_t)(sizeof(extHist) / sizeof(extHist[0]))) extHistCount++;
}

/**
 * @brief Find a reference sample that is at least windowMs old.
 * @param[in]  nowMs    Current millis() value.
 * @param[in]  windowMs Minimum age in milliseconds.
 * @param[out] out      The found sample.
 * @return true if a suitable sample was found.
 */
static bool findRefSample(uint32_t nowMs, uint32_t windowMs, ExtSample& out) {
  if (extHistCount < 2) return false;

  // Search oldest-to-newest for the newest sample that is at least windowMs old.
  // Buffer order: head points to next write, so oldest is at (head - count).
  int cap   = (int)(sizeof(extHist) / sizeof(extHist[0]));
  int start = (int)extHistHead - (int)extHistCount;
  if (start < 0) start += cap;

  bool found = false;
  ExtSample best = extHist[start];
  for (uint8_t i = 0; i < extHistCount; i++) {
    int idx            = (start + i) % cap;
    const ExtSample& s = extHist[idx];
    if (nowMs - s.ms >= windowMs) {
      best  = s;
      found = true;
    }
  }
  if (!found) return false;
  out = best;

  return true;
}

/**
 * @brief Calculate trend direction based on delta and threshold.
 * @return -1 (falling), 0 (steady), or +1 (rising).
 */
static int8_t calcTrend(float current, float ref, float threshold) {
  float d = current - ref;

  if (d >= threshold) return 1;
  if (d <= -threshold) return -1;

  return 0;
}

/**
 * @brief Parse float from a span (non-null-terminated) substring.
 *
 * Copies the span into a small temporary buffer, then uses strtof().
 */
static bool parseFloatSpan(const char* start, size_t len, float& out) {
  // Copy into a small buffer to use strtof safely.
  // Values in gauge.txt are short (e.g. "-12.3").
  if (len == 0) return false;
  if (len >= 32) len = 31;
  char buf[32];
  memcpy(buf, start, len);
  buf[len] = '\0';

  char* endptr = nullptr;
  out          = strtof(buf, &endptr);

  return endptr != buf;
}

/**
 * @brief Parse gauge CSV payload.
 *
 * Mapping (kept compatible with previous implementation):
 * - field0 = timestamp
 * - field1 = temperature
 * - field4 = pressure
 * - field5 = humidity
 */
static bool parseGaugeCsv(const String& payload, float& outTemp, float& outPress, float& outHum) {
  // Expected mapping from existing code:
  // field0 = timestamp, field1 = temperature, field4 = pressure, field5 = humidity
  const char* s          = payload.c_str();
  const char* fieldStart = s;
  int field              = 0;

  bool gotTemp  = false;
  bool gotPress = false;
  bool gotHum   = false;

  for (const char* p = s;; p++) {
    char c     = *p;
    bool atEnd = (c == '\0' || c == '\n' || c == '\r');
    if (c == ',' || atEnd) {
      size_t fieldLen = (size_t)(p - fieldStart);
      if (field == 1) {
        gotTemp = parseFloatSpan(fieldStart, fieldLen, outTemp);
      } else if (field == 4) {
        gotPress = parseFloatSpan(fieldStart, fieldLen, outPress);
      } else if (field == 5) {
        gotHum = parseFloatSpan(fieldStart, fieldLen, outHum);
      }

      if (atEnd) break;
      field++;
      fieldStart = p + 1;
    }
  }

  return gotTemp && gotPress && gotHum;
}

/**
 * @brief Update outdoor trend indicators from history buffer.
 */
static void updateExtTrends() {
  const uint32_t windowMs = (uint32_t)TREND_WINDOW_MINUTES * 60UL * 1000UL;
  ExtSample ref;
  if (!findRefSample(millis(), windowMs, ref)) {
    // Not enough history yet (or history window not covered).
    // Keep the last computed trends instead of forcing "no change".
    // This prevents the UI from flickering between an arrow and a dash.
    return;
  }

  // Practical thresholds (avoid noise)
  extTempTrend  = calcTrend(extTemperature, ref.t, 0.3f);
  extHumTrend   = calcTrend(extHumidity, ref.h, 2.0f);
  extPressTrend = calcTrend(extPressure, ref.p, 0.8f);
}

/**
 * @brief Fetch and parse outdoor readings from meter.ac gauge endpoint.
 * @return true if data was successfully fetched and parsed.
 */
bool fetchGaugeData() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(CFG_GAUGE_HTTP_TIMEOUT_MS);
  if (!https.begin(client, CFG_GAUGE_URL)) {
    LOG_E("Gauge: begin() failed");
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_W("Gauge: HTTP %d", httpCode);
    https.end();
    return false;
  }

  String payload = https.getString();
  https.end();

  float t = 0.0f;
  float p = 0.0f;
  float h = 0.0f;
  if (!parseGaugeCsv(payload, t, p, h)) {
    LOG_E("Gauge: CSV parse failed");
    return false;
  }

  extTemperature = t;
  extPressure    = p;
  extHumidity    = h;
  haveExtData    = true;

  pushExtSample(extTemperature, extHumidity, extPressure);
  updateExtTrends();

  return true;
}

/**
 * @brief Fetch daily forecast from Open-Meteo API.
 *
 * Populates the global forecast[] array with the next 3 days
 * (skipping today).
 *
 * @return true if at least one forecast day was parsed.
 */
bool fetchForecast() {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(CFG_TLS_RX_BUFFER_BYTES_FORECAST, CFG_TLS_TX_BUFFER_BYTES_FORECAST);

  HTTPClient https;
  if (!https.begin(client, CFG_FORECAST_URL)) {
    LOG_E("Forecast: begin() failed");
    return false;
  }

  https.useHTTP10(true);
  https.addHeader("Accept-Encoding", "identity");

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_W("Forecast: HTTP %d", httpCode);
    https.end();
    return false;
  }

  StaticJsonDocument<256> filter;
  JsonObject dailyFilter            = filter["daily"].to<JsonObject>();
  dailyFilter["time"]               = true;
  dailyFilter["weather_code"]       = true;
  dailyFilter["temperature_2m_max"] = true;
  dailyFilter["temperature_2m_min"] = true;
  dailyFilter["precipitation_sum"]  = true;
  dailyFilter["wind_speed_10m_max"] = true;
  dailyFilter["cloud_cover_mean"]   = true;

  DynamicJsonDocument doc(CFG_FORECAST_JSON_DOC_CAPACITY);
  WiFiClient* stream       = https.getStreamPtr();
  DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  https.end();

  if (err) {
    LOG_E("Forecast: JSON error - %s", err.c_str());
    return false;
  }

  JsonArray times     = doc["daily"]["time"].as<JsonArray>();
  JsonArray codes     = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray tMaxArr   = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray tMinArr   = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray precipArr = doc["daily"]["precipitation_sum"].as<JsonArray>();
  JsonArray windArr   = doc["daily"]["wind_speed_10m_max"].as<JsonArray>();
  JsonArray cloudArr  = doc["daily"]["cloud_cover_mean"].as<JsonArray>();

  if (!times || !codes || !tMaxArr || !tMinArr || !precipArr || !windArr || !cloudArr) {
    LOG_E("Forecast: missing arrays in response");
    return false;
  }

  forecastCount = 0;
  for (int i = 0; i < 3; i++) forecast[i].valid = false;

  // Skip index 0 (today), take the next 3 days if available.
  for (size_t src = 1; src < times.size() && forecastCount < 3; src++) {
    const char* t = times[src];
    if (!t) continue;

    strncpy(forecast[forecastCount].label, t, sizeof(forecast[forecastCount].label) - 1);
    forecast[forecastCount].label[sizeof(forecast[forecastCount].label) - 1] = '\0';
    forecast[forecastCount].wmoCode   = codes[src].as<int>();
    forecast[forecastCount].tMax      = tMaxArr[src].as<float>();
    forecast[forecastCount].tMin      = tMinArr[src].as<float>();
    forecast[forecastCount].precip    = precipArr[src].as<float>();
    forecast[forecastCount].windMax   = windArr[src].as<float>();
    forecast[forecastCount].cloudMean = cloudArr[src].as<float>();
    forecast[forecastCount].valid     = true;
    forecastCount++;
  }

  LOG_I("Forecast: %d days loaded", forecastCount);
  return forecastCount > 0;
}
