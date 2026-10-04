#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <cstring>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include "project_config.h"
#include "tls_roots.h"

#include "data.h"
#include "debug.h"

// Fallback defaults if config constants are missing (CI without config.h).
#ifndef CFG_GAUGE_HTTP_TIMEOUT_MS
  #define CFG_GAUGE_HTTP_TIMEOUT_MS 3500UL
#endif
#ifndef CFG_GAUGE_URL
  #define CFG_GAUGE_URL "https://meter.ac/gs/nodes/N200/gauge.txt"
#endif
#ifndef CFG_FORECAST_URL
  #define CFG_FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=42.1859191&longitude=24.3398302&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,wind_speed_10m_max,cloud_cover_mean&forecast_days=4&models=ecmwf_ifs&timezone=auto"
#endif
#ifndef CFG_FORECAST_JSON_DOC_CAPACITY
  #define CFG_FORECAST_JSON_DOC_CAPACITY 2048
#endif
#ifndef CFG_HOURLY_FORECAST_URL
  #define CFG_HOURLY_FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=42.1859191&longitude=24.3398302&hourly=temperature_2m,precipitation,weather_code,cloud_cover,wind_speed_10m,is_day&forecast_days=1&models=ecmwf_ifs&timezone=auto"
#endif
#ifndef CFG_HOURLY_JSON_DOC_CAPACITY
  #define CFG_HOURLY_JSON_DOC_CAPACITY 4000
#endif

// Trend window is configured in config.h (minutes).
// We expect it as a macro (so every translation unit sees the same value).
// If it's missing, keep a safe default.
#ifndef TREND_WINDOW_MINUTES
#define TREND_WINDOW_MINUTES 30
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

ForecastBlock todayBlocks[4];
bool haveTodayForecast = false;
char todayForecastDate[11] = {};
time_t gaugeObservationEpoch = 0;

// Limit total body size and elapsed time, including a peer that sends one byte
// just before each ordinary socket timeout. ArduinoJson supports custom readers.
class ResponseReader {
 public:
  ResponseReader(WiFiClient& client, size_t limit, uint32_t idleTimeout)
      : client_(client), limit_(limit), idleTimeout_(idleTimeout), started_(millis()) {}
  int read() {
    const uint32_t waitingSince = millis();
    while (true) {
      if (millis() - started_ >= CFG_HTTP_TOTAL_TIMEOUT_MS) { failed = true; return -1; }
      if (client_.available()) {
        if (count_ >= limit_) { failed = true; return -1; }
        int value = client_.read();
        if (value >= 0) { ++count_; return value; }
      } else if (!client_.connected()) {
        return -1;
      }
      if (millis() - waitingSince >= idleTimeout_) { failed = true; return -1; }
      delay(1);
    }
  }
  size_t readBytes(char* buffer, size_t length) {
    size_t n = 0;
    while (n < length) { int c = read(); if (c < 0) break; buffer[n++] = (char)c; }
    return n;
  }
  bool failed = false;
 private:
  WiFiClient& client_;
  size_t limit_, count_ = 0;
  uint32_t idleTimeout_, started_;
};

static uint16_t tlsRxSizes[3] = {};

// HTTPClient builds header lines as Strings. Bound that stage as well as the
// body, so a huge header or a slow trickle cannot consume unlimited heap/time.
class LimitedSecureClient : public BearSSL::WiFiClientSecure {
 public:
  void beginHeaders() { headers_ = true; bytes_ = 0; started_ = millis(); }
  void endHeaders() { headers_ = false; }
  int available() override { return allowed() ? BearSSL::WiFiClientSecure::available() : 0; }
  uint8_t connected() override { return allowed() ? BearSSL::WiFiClientSecure::connected() : 0; }
  int read() override {
    if (!allowed()) return -1;
    const int value = BearSSL::WiFiClientSecure::read();
    if (headers_ && value >= 0) ++bytes_;
    return value;
  }
  int read(uint8_t* buffer, size_t size) override {
    if (!allowed()) return -1;
    if (headers_ && size > CFG_HTTP_MAX_HEADER_BYTES - bytes_) size = CFG_HTTP_MAX_HEADER_BYTES - bytes_;
    const int count = BearSSL::WiFiClientSecure::read(buffer, size);
    if (headers_ && count > 0) bytes_ += (size_t)count;
    return count;
  }
 private:
  bool allowed() {
    if (headers_ && (bytes_ >= CFG_HTTP_MAX_HEADER_BYTES || millis() - started_ >= CFG_HTTP_TOTAL_TIMEOUT_MS)) {
      BearSSL::WiFiClientSecure::stop();
      return false;
    }
    return true;
  }
  bool headers_ = false;
  size_t bytes_ = 0;
  uint32_t started_ = 0;
};

static bool beginSecureHttp(LimitedSecureClient& client, HTTPClient& https,
                            const char* url, uint16_t timeout, uint8_t endpoint) {
  const time_t now = time(nullptr);
  if (now < 1609459200 || strncmp(url, "https://", 8) != 0) {
    LOG_W("HTTPS: valid NTP time and an https:// URL are required");
    return false;
  }
  static const BearSSL::X509List roots(TLS_ROOT_CERTIFICATES);
  client.setTrustAnchors(&roots);
  client.setX509Time(now);
  client.setTimeout(timeout);

  // A small RX buffer is safe only after the server negotiates MFLN. Cache the
  // probe per configured endpoint; servers without support use full TLS records.
  if (!tlsRxSizes[endpoint]) {
    String address(url + 8);
    int slash = address.indexOf('/');
    if (slash >= 0) address.remove(slash);
    uint16_t port = 443;
    int colon = address.indexOf(':');
    if (colon >= 0) {
      long configuredPort = address.substring(colon + 1).toInt();
      if (configuredPort <= 0 || configuredPort > 65535) return false;
      port = (uint16_t)configuredPort;
      address.remove(colon);
    }
    tlsRxSizes[endpoint] = BearSSL::WiFiClientSecure::probeMaxFragmentLength(address.c_str(), port, 2048)
        ? 2048 : 16384;
    LOG_I("HTTPS: endpoint %u RX buffer %u", endpoint, tlsRxSizes[endpoint]);
  }
  client.setBufferSizes(tlsRxSizes[endpoint], 512);
  https.setTimeout(timeout);
  if (!https.begin(client, url)) return false;
  https.useHTTP10(true); // unchunked body for direct stream parsing
  https.addHeader("Accept-Encoding", "identity");
  return true;
}

static int secureGet(LimitedSecureClient& client, HTTPClient& https, uint8_t endpoint) {
  client.beginHeaders();
  const int code = https.GET();
  client.endHeaders();
  if (code < 0) {
    char error[96] = {};
    const int sslError = client.getLastSSLError(error, sizeof(error));
    LOG_W("HTTPS: HTTP %d, TLS %d (%s)", code, sslError, error);
  }
  if (tlsRxSizes[endpoint] < 16384 && !client.getMFLNStatus()) {
    tlsRxSizes[endpoint] = 16384; // server policy changed: use full records on next scheduled attempt
    LOG_W("HTTPS: MFLN unavailable; next attempt will use full RX buffer");
    return -1;
  }
  if (code == HTTP_CODE_OK) {
    LOG_I("HTTPS: endpoint %u verified; free heap %u", endpoint, ESP.getFreeHeap());
  }
  return code;
}

static bool inRange(float value, float low, float high) {
  return isfinite(value) && value >= low && value <= high;
}

static bool validWmo(int code) {
  switch (code) {
    case 0: case 1: case 2: case 3: case 45: case 48:
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 71: case 73: case 75: case 77: case 80: case 81: case 82:
    case 85: case 86: case 95: case 96: case 99: return true;
    default: return false;
  }
}

static bool localDate(char* out, int daysAhead = 0) {
  time_t now = time(nullptr);
  if (now < 1609459200) return false;
  tm ti;
  localtime_r(&now, &ti);
  ti.tm_hour = 12; ti.tm_min = 0; ti.tm_sec = 0;
  ti.tm_mday += daysAhead; ti.tm_isdst = -1;
  if (mktime(&ti) == (time_t)-1) return false;
  return strftime(out, 11, "%Y-%m-%d", &ti) == 10;
}

static bool numericValue(JsonVariantConst value, float& out, float low, float high) {
  if (!value.is<float>()) return false;
  out = value.as<float>();
  return inRange(out, low, high);
}

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

// Fixed buffer size - sample interval adapts to TREND_WINDOW_MINUTES.
// 10 samples means oldest sample is always ~90% of the trend window.
static constexpr uint8_t TREND_BUFFER_SIZE = 10;
static ExtSample extHist[TREND_BUFFER_SIZE];
static uint8_t extHistCount                = 0;
static uint8_t extHistHead                 = 0;
static uint32_t lastTrendSampleMs          = 0;

/**
 * @brief Calculate the adaptive sample interval based on trend window.
 * @return Interval in milliseconds between trend samples.
 */
static uint32_t getTrendSampleIntervalMs() {
  // Interval = window / buffer_size
  // E.g., 30 min window / 10 samples = 3 min interval
  return ((uint32_t)TREND_WINDOW_MINUTES * 60UL * 1000UL) / TREND_BUFFER_SIZE;
}

/**
 * @brief Push a new outdoor sample into the ring buffer.
 */
static void pushExtSample(float t, float h, float p) {
  extHist[extHistHead] = { millis(), t, h, p };
  extHistHead          = (extHistHead + 1) % TREND_BUFFER_SIZE;
  if (extHistCount < TREND_BUFFER_SIZE) extHistCount++;
}

/**
 * @brief Get the oldest sample in the ring buffer.
 * @param[out] out The oldest sample.
 * @return true if buffer has at least 2 samples.
 *
 * With adaptive sampling, the oldest sample is always close to the trend window age,
 * so we simply return it without searching for a specific time threshold.
 */
static bool getOldestSample(ExtSample& out) {
  if (extHistCount < 2) return false;

  // Buffer order: head points to next write, so oldest is at (head - count).
  int start = (int)extHistHead - (int)extHistCount;
  if (start < 0) start += TREND_BUFFER_SIZE;

  out = extHist[start];
  const uint32_t age = millis() - out.ms;
  const uint32_t window = (uint32_t)TREND_WINDOW_MINUTES * 60000UL;
  if (age > window + getTrendSampleIntervalMs()) return false;
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
 * Mapping (kept compatible with previous implementation):
 * - field0 = timestamp
 * - field1 = temperature
 * - field4 = pressure
 * - field5 = humidity
 */
static bool parseGaugeCsv(const char* payload, float& outTemp, float& outPress, float& outHum, time_t& observed) {
  // Expected mapping from existing code:
  // field0 = timestamp, field1 = temperature, field4 = pressure, field5 = humidity
  const char* s          = payload;
  const char* fieldStart = s;
  int field              = 0;

  bool gotTemp  = false;
  bool gotPress = false;
  bool gotHum   = false;
  bool gotTime  = false;

  for (const char* p = s;; p++) {
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
          const time_t now = time(nullptr);
          gotTime = gotTime && epoch >= 1609459200 && epoch <= (uint64_t)now + 300;
          if (gotTime) observed = (time_t)epoch;
        }
      } else if (field == 1) {
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

  return gotTime && gotTemp && gotPress && gotHum &&
         inRange(outTemp, -80, 65) && inRange(outPress, 800, 1100) && inRange(outHum, 0, 100);
}

/**
 * @brief Update outdoor trend indicators from history buffer.
 */
static void updateExtTrends() {
  ExtSample ref;
  if (!getOldestSample(ref)) {
    // Not enough history yet.
    // Keep the last computed trends instead of forcing "no change".
    extTempTrend = extHumTrend = extPressTrend = 0;
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
  LimitedSecureClient client;
  HTTPClient https;
  if (!beginSecureHttp(client, https, CFG_GAUGE_URL, CFG_GAUGE_HTTP_TIMEOUT_MS, 0)) {
    LOG_E("Gauge: begin() failed");
    return false;
  }

  int httpCode = secureGet(client, https, 0);
  if (httpCode != HTTP_CODE_OK) {
    LOG_W("Gauge: HTTP %d", httpCode);
    https.end();
    return false;
  }

  if (https.getSize() > CFG_GAUGE_MAX_RESPONSE_BYTES) { https.end(); return false; }
  char payload[CFG_GAUGE_MAX_RESPONSE_BYTES + 1];
  ResponseReader reader(*https.getStreamPtr(), CFG_GAUGE_MAX_RESPONSE_BYTES, CFG_GAUGE_HTTP_TIMEOUT_MS);
  size_t length = 0;
  int c;
  while ((c = reader.read()) >= 0) {
    if (c == 0) { reader.failed = true; break; }
    payload[length++] = (char)c;
  }
  payload[length] = '\0';
  const int declaredSize = https.getSize();
  https.end();
  if (reader.failed || (declaredSize >= 0 && length != (size_t)declaredSize)) return false;

  float t = 0.0f;
  float p = 0.0f;
  float h = 0.0f;
  time_t observed = 0;
  if (!parseGaugeCsv(payload, t, p, h, observed)) {
    LOG_E("Gauge: CSV parse failed");
    return false;
  }

  if (haveExtData && observed < gaugeObservationEpoch) return false;

  extTemperature = t;
  extPressure    = p;
  extHumidity    = h;
  haveExtData    = true;
  const bool newObservation = observed > gaugeObservationEpoch;
  gaugeObservationEpoch = observed;

  // Adaptive sampling: only store a sample if enough time has passed.
  // This ensures the buffer covers the full trend window regardless of fetch frequency.
  const uint32_t now            = millis();
  const uint32_t sampleInterval = getTrendSampleIntervalMs();
  if (newObservation && time(nullptr) - observed <= (time_t)TREND_WINDOW_MINUTES * 60 &&
      (extHistCount == 0 || (now - lastTrendSampleMs) >= sampleInterval)) {
    pushExtSample(extTemperature, extHumidity, extPressure);
    lastTrendSampleMs = now;
  }

  updateExtTrends();
  LOG_I("Gauge: %.1f C, %.0f%%, %.1f hPa", t, h, p);

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
  LimitedSecureClient client;
  HTTPClient https;
  if (!beginSecureHttp(client, https, CFG_FORECAST_URL, CFG_FORECAST_HTTP_TIMEOUT_MS, 1)) {
    LOG_E("Forecast: begin() failed");
    return false;
  }

  int httpCode = secureGet(client, https, 1);
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
  if (https.getSize() > CFG_FORECAST_MAX_RESPONSE_BYTES) { https.end(); return false; }
  ResponseReader reader(*https.getStreamPtr(), CFG_FORECAST_MAX_RESPONSE_BYTES, CFG_FORECAST_HTTP_TIMEOUT_MS);
  DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  https.end();

  if (err || reader.failed) {
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

  const size_t count = times.size();
  if (count < 4 || count > 16 || codes.size() != count || tMaxArr.size() != count ||
      tMinArr.size() != count || precipArr.size() != count || windArr.size() != count || cloudArr.size() != count) return false;

  ForecastDay next[3]{};
  for (size_t src = 1; src <= 3; ++src) {
    ForecastDay& day = next[src - 1];
    char expectedDate[11];
    const char* date = times[src].as<const char*>();
    if (!localDate(expectedDate, (int)src) || !date || strcmp(date, expectedDate) != 0 ||
        !codes[src].is<int>() || !validWmo(codes[src].as<int>()) ||
        !numericValue(tMaxArr[src], day.tMax, -80, 65) ||
        !numericValue(tMinArr[src], day.tMin, -80, 65) || day.tMin > day.tMax ||
        !numericValue(precipArr[src], day.precip, 0, 2000) ||
        !numericValue(windArr[src], day.windMax, 0, 500) ||
        !numericValue(cloudArr[src], day.cloudMean, 0, 100)) {
      LOG_W("Forecast: invalid date or values");
      return false;
    }
    memcpy(day.label, date, sizeof(day.label));
    day.wmoCode = codes[src].as<int>();
    day.valid = true;
  }
  memcpy(forecast, next, sizeof(next));
  forecastCount = 3;

  LOG_I("Forecast: %d days loaded", forecastCount);
  return forecastCount > 0;
}

/**
 * @brief Return the "severity" of a WMO weather code (higher = worse).
 *
 * Used to pick the most significant code within a 6-hour block.
 */
static int wmoSeverity(int code) {
  if (code == 95 || code == 96 || code == 99) return 5; // thunderstorm
  if (code >= 61 && code <= 67) return 4;               // rain moderate/heavy
  if (code >= 71 && code <= 77) return 4;               // snow moderate/heavy
  if (code >= 80 && code <= 82) return 4;               // showers
  if (code >= 51 && code <= 57) return 3;               // drizzle
  if (code >= 85 && code <= 86) return 3;               // snow showers
  if (code == 3) return 2;                              // overcast
  if (code == 2) return 1;                              // partly cloudy
  if (code == 1) return 1;                              // mainly clear
  return 0;                                             // clear
}

/**
 * @brief Fetch today's hourly forecast and aggregate into 4 six-hour blocks.
 * @return true if at least one block was populated.
 */
bool fetchHourlyForecast() {
  LimitedSecureClient client;
  HTTPClient https;
  if (!beginSecureHttp(client, https, CFG_HOURLY_FORECAST_URL, CFG_FORECAST_HTTP_TIMEOUT_MS, 2)) {
    LOG_E("Hourly: begin() failed");
    return false;
  }

  int httpCode = secureGet(client, https, 2);
  if (httpCode != HTTP_CODE_OK) {
    LOG_W("Hourly: HTTP %d", httpCode);
    https.end();
    return false;
  }

  StaticJsonDocument<256> filter;
  JsonObject hourlyFilter             = filter["hourly"].to<JsonObject>();
  hourlyFilter["time"]                = true;
  hourlyFilter["temperature_2m"]      = true;
  hourlyFilter["precipitation"]       = true;
  hourlyFilter["weather_code"]        = true;
  hourlyFilter["cloud_cover"]         = true;
  hourlyFilter["wind_speed_10m"]      = true;
  hourlyFilter["is_day"]              = true;

  DynamicJsonDocument doc(CFG_HOURLY_JSON_DOC_CAPACITY);
  if (https.getSize() > CFG_HOURLY_MAX_RESPONSE_BYTES) { https.end(); return false; }
  ResponseReader reader(*https.getStreamPtr(), CFG_HOURLY_MAX_RESPONSE_BYTES, CFG_FORECAST_HTTP_TIMEOUT_MS);
  DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  https.end();

  if (err || reader.failed) {
    LOG_E("Hourly: JSON error - %s", err.c_str());
    return false;
  }

  JsonArray tempArr  = doc["hourly"]["temperature_2m"].as<JsonArray>();
  JsonArray precArr  = doc["hourly"]["precipitation"].as<JsonArray>();
  JsonArray codeArr  = doc["hourly"]["weather_code"].as<JsonArray>();
  JsonArray cloudArr = doc["hourly"]["cloud_cover"].as<JsonArray>();
  JsonArray windArr  = doc["hourly"]["wind_speed_10m"].as<JsonArray>();
  // Optional: older URLs in config.h may not request is_day.
  JsonArray dayArr   = doc["hourly"]["is_day"].as<JsonArray>();
  JsonArray timeArr  = doc["hourly"]["time"].as<JsonArray>();

  if (!tempArr || !precArr || !codeArr || !cloudArr || !windArr || !timeArr) {
    LOG_E("Hourly: missing arrays in response");
    return false;
  }

  const size_t count = tempArr.size();
  if (count < 23 || count > 25 || timeArr.size() != count || precArr.size() != count ||
      codeArr.size() != count || cloudArr.size() != count || windArr.size() != count ||
      (dayArr && dayArr.size() != count)) return false;
  char expectedDate[11];
  if (!localDate(expectedDate)) return false;
  ForecastBlock next[4]{};
  // Build a temporary snapshot; a malformed response cannot erase good data.
  for (int b = 0; b < 4; b++) {
    next[b].tMin = 999.0f;
    next[b].tMax = -999.0f;
  }

  int blockCounts[4] = {0, 0, 0, 0};
  int worstSeverity[4] = {0, 0, 0, 0};
  int dayHours[4]      = {0, 0, 0, 0};

  // Aggregate hourly values into 6-hour blocks
  int previousHour = -1;
  int repeatedHours = 0, skippedHours = 0;
  for (size_t h = 0; h < count; h++) {
    const char* stamp = timeArr[h].as<const char*>();
    if (!stamp || strlen(stamp) != 16 || memcmp(stamp, expectedDate, 10) != 0 ||
        stamp[10] != 'T' || stamp[11] < '0' || stamp[11] > '2' ||
        stamp[12] < '0' || stamp[12] > '9' || strcmp(stamp + 13, ":00") != 0) return false;
    const int hour = (stamp[11] - '0') * 10 + stamp[12] - '0';
    if (hour > 23 || hour < previousHour || hour > previousHour + 2 ||
        (hour == previousHour && count != 25) || (h == 0 && hour != 0)) return false;
    if (hour == previousHour) ++repeatedHours;
    if (previousHour >= 0 && hour == previousHour + 2) ++skippedHours;
    previousHour = hour;
    int block = hour / 6;
    float t, p, cl, w;
    if (!numericValue(tempArr[h], t, -80, 65) || !numericValue(precArr[h], p, 0, 500) ||
        !numericValue(cloudArr[h], cl, 0, 100) || !numericValue(windArr[h], w, 0, 500) ||
        !codeArr[h].is<int>() || !validWmo(codeArr[h].as<int>()) ||
        (dayArr && (!dayArr[h].is<int>() || (dayArr[h].as<int>() != 0 && dayArr[h].as<int>() != 1)))) return false;
    int c = codeArr[h].as<int>();
    // Daylight: from the API when available, otherwise a rough 07:00-19:00 fallback.
    bool isDay = dayArr ? (dayArr[h].as<int>() != 0) : (hour >= 7 && hour < 19);
    if (isDay) dayHours[block]++;

    if (t < next[block].tMin) next[block].tMin = t;
    if (t > next[block].tMax) next[block].tMax = t;
    next[block].precip += p;
    if (w > next[block].windMax) next[block].windMax = w;
    next[block].cloudMean += cl;

    int sev = wmoSeverity(c);
    if (sev > worstSeverity[block]) {
      worstSeverity[block] = sev;
      next[block].wmoCode = c;
    }

    blockCounts[block]++;
    next[block].valid = true;
  }

  // Average out cloud cover
  int validBlocks = 0;
  for (int b = 0; b < 4; b++) {
    if (blockCounts[b] > 0) {
      next[b].cloudMean /= (float)blockCounts[b];
      next[b].night = (dayHours[b] * 2 < blockCounts[b]);
      validBlocks++;
    }
  }

  if (validBlocks != 4 || previousHour != 23 || repeatedHours != (count == 25 ? 1 : 0) ||
      skippedHours != (count == 23 ? 1 : 0)) return false;
  memcpy(todayBlocks, next, sizeof(next));
  memcpy(todayForecastDate, expectedDate, sizeof(todayForecastDate));
  haveTodayForecast = true;
  LOG_I("Hourly: %d blocks loaded", validBlocks);
  return haveTodayForecast;
}
