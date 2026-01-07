#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <cstring>
#include <stdlib.h>

#include "config.h"
#include "data.h"

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

// --- Outdoor trend history (simple ring buffer) ---
struct ExtSample {
  uint32_t ms;
  float t;
  float h;
  float p;
};

static ExtSample extHist[16];
static uint8_t extHistCount = 0;
static uint8_t extHistHead  = 0; // next write index

static void pushExtSample(float t, float h, float p) {
  extHist[extHistHead] = { millis(), t, h, p };
  extHistHead = (extHistHead + 1) % (uint8_t)(sizeof(extHist) / sizeof(extHist[0]));
  if (extHistCount < (uint8_t)(sizeof(extHist) / sizeof(extHist[0]))) extHistCount++;
}

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
 * @brief Fetch and parse outdoor readings from meter.ac.
 */
bool fetchGaugeData() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(7000);
  if (!https.begin(client, "https://meter.ac/gs/nodes/N200/gauge.txt")) {
    Serial.println("Gauge begin() failed");

    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Gauge HTTP code: %d\n", httpCode);
    https.end();

    return false;
  }

  String payload = https.getString();
  https.end();

  float t = 0.0f;
  float p = 0.0f;
  float h = 0.0f;
  if (!parseGaugeCsv(payload, t, p, h)) {
    Serial.println("Gauge parse failed (csv)");

    return false;
  }

  extTemperature = t;
  extPressure    = p;
  extHumidity    = h;
  haveExtData    = true;

  pushExtSample(extTemperature, extHumidity, extPressure);
  updateExtTrends();
  Serial.println("Gauge fetch OK");

  return true;
}

/**
 * @brief Fetch daily forecast from Open-Meteo and fill forecast[] (tomorrow + next 2).
 */
bool fetchForecast() {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(2048, 512);

  HTTPClient https;
  // Full request as provided (hourly included). We'll parse only daily parts but keep payload intact.
  const char* url = "https://api.open-meteo.com/v1/forecast?latitude=42.1859191&longitude=24.3398302&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,wind_speed_10m_max,cloud_cover_mean&models=ecmwf_ifs&timezone=auto";

  if (!https.begin(client, url)) {
    Serial.println("Forecast begin() failed");

    return false;
  }

  https.useHTTP10(true); // avoid chunked/gzip surprises on ESP8266
  https.addHeader("Accept-Encoding", "identity");

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Forecast HTTP code: %d\n", httpCode);
    https.end();

    return false;
  }

  StaticJsonDocument<256> filter;
  JsonObject dailyFilter = filter["daily"].to<JsonObject>();
  dailyFilter["time"]               = true;
  dailyFilter["weather_code"]       = true;
  dailyFilter["temperature_2m_max"] = true;
  dailyFilter["temperature_2m_min"] = true;
  dailyFilter["precipitation_sum"]  = true;
  dailyFilter["wind_speed_10m_max"] = true;
  dailyFilter["cloud_cover_mean"]   = true;
  DynamicJsonDocument doc(13000);
  WiFiClient* stream       = https.getStreamPtr();
  DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  https.end();

  if (err) {
    Serial.print("Forecast JSON error: ");
    Serial.println(err.c_str());

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
    Serial.println("Forecast arrays missing");

    return false;
  }

  Serial.printf("Forecast days available: %u\n", times.size());

  forecastCount = 0;
  for (int i = 0; i < 3; i++) forecast[i].valid = false;

  // skip index 0 (today), take the next three days if available
  for (size_t src = 1; src < times.size() && forecastCount < 3; src++) {
    const char* t = times[src];
    if (!t) continue;
    forecast[forecastCount].label     = String(t);
    forecast[forecastCount].wmoCode   = codes[src].as<int>();
    forecast[forecastCount].tMax      = tMaxArr[src].as<float>();
    forecast[forecastCount].tMin      = tMinArr[src].as<float>();
    forecast[forecastCount].precip    = precipArr[src].as<float>();
    forecast[forecastCount].windMax   = windArr[src].as<float>();
    forecast[forecastCount].cloudMean = cloudArr[src].as<float>();
    forecast[forecastCount].valid     = true;
    forecastCount++;
  }

  return forecastCount > 0;
}
