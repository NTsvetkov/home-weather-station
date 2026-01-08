#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <math.h>
#include <cstring>
#include <time.h>

// Local secrets are expected in src/config.h (ignored by git).
// CI/builds without secrets should still compile, so fall back to config.example.h.
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#elif __has_include("config.example.h")
#include "config.example.h"
#endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// Timezone (POSIX TZ string). Override in config.h if needed.
// Bulgaria: EET (UTC+2) with EEST (UTC+3) DST.
#ifndef TZ_INFO
#define TZ_INFO "EET-2EEST,M3.5.0/3,M10.5.0/4"
#endif

// ---- Tunables (fallbacks if not present in config.h) ----
#ifndef CFG_MAIN_SCREEN_DURATION_MS
#define CFG_MAIN_SCREEN_DURATION_MS 10000UL
#endif
#ifndef CFG_FORECAST_SCREEN_DURATION_MS
#define CFG_FORECAST_SCREEN_DURATION_MS 10000UL
#endif
#ifndef CFG_INTERNAL_READ_INTERVAL_MS
#define CFG_INTERNAL_READ_INTERVAL_MS 2000UL
#endif
#ifndef CFG_WIFI_RECONNECT_INTERVAL_MS
#define CFG_WIFI_RECONNECT_INTERVAL_MS 15000UL
#endif

#ifndef CFG_TEMP_DELTA_C
#define CFG_TEMP_DELTA_C 0.2f
#endif
#ifndef CFG_HUM_DELTA_PCT
#define CFG_HUM_DELTA_PCT 1.0f
#endif

#ifndef CFG_STARTUP_GAUGE_RETRY_INTERVAL_MS
#define CFG_STARTUP_GAUGE_RETRY_INTERVAL_MS 2000UL
#endif
#ifndef CFG_STARTUP_GAUGE_MAX_ATTEMPTS
#define CFG_STARTUP_GAUGE_MAX_ATTEMPTS 5
#endif

#ifndef CFG_GAUGE_FETCH_INTERVAL_MS
#define CFG_GAUGE_FETCH_INTERVAL_MS 180000UL
#endif
#ifndef CFG_GAUGE_RETRY_INTERVAL_MS
#define CFG_GAUGE_RETRY_INTERVAL_MS 30000UL
#endif
#ifndef CFG_FORECAST_FETCH_INTERVAL_MS
#define CFG_FORECAST_FETCH_INTERVAL_MS 3600000UL
#endif
#ifndef CFG_FORECAST_RETRY_INTERVAL_MS
#define CFG_FORECAST_RETRY_INTERVAL_MS 300000UL
#endif

// Timekeeping
// Sync our millis-based epoch estimate from system time (NTP) occasionally.
#ifndef CFG_TIME_SYNC_INTERVAL_MS
#define CFG_TIME_SYNC_INTERVAL_MS 3600000UL
#endif
// While NTP is not yet valid after boot, retry syncing more frequently.
#ifndef CFG_TIME_SYNC_RETRY_MS
#define CFG_TIME_SYNC_RETRY_MS 5000UL
#endif
// Check local date (midnight rollover detection) at this cadence.
#ifndef CFG_DATE_CHECK_INTERVAL_MS
#define CFG_DATE_CHECK_INTERVAL_MS 60000UL
#endif

#include "data.h"
#include "debug.h"
#include "display.h"
#include "sensors.h"

/* -------------------------------------------------------------------------- */
/*                              State Variables                               */
/* -------------------------------------------------------------------------- */

/** @brief Last millis() when gauge fetch was attempted. */
uint32_t lastGaugeAttemptMs         = 0;
/** @brief Last millis() when gauge fetch succeeded. */
uint32_t lastGaugeSuccessMs         = 0;
/** @brief Last millis() when forecast fetch was attempted. */
uint32_t lastForecastFetchMs        = 0;
/** @brief Last millis() when forecast fetch succeeded. */
uint32_t lastForecastSuccessMs      = 0;
uint32_t lastScreenSwitchMs         = 0;
uint32_t currentScreenDuration      = 0;
uint32_t lastIntReadMs              = 0;
bool showMainScreen                 = true;
bool needRedraw                     = true;

static bool midnightForecastPending = false;
static char lastNtpDate[11]         = {0};
static bool lastNtpDateValid        = false;
static bool gaugeRetryPending       = true;

static bool wifiConfigured          = false;
static uint32_t lastWifiAttemptMs   = 0;
static bool ntpConfigured           = false;
static bool startupGaugePending     = true;
static bool firstExtRedrawDone      = false;
static bool extDataRedrawPending    = false;
static uint8_t startupGaugeAttempts = 0;

const uint32_t STARTUP_GAUGE_RETRY_INTERVAL_MS = (uint32_t)CFG_STARTUP_GAUGE_RETRY_INTERVAL_MS;
const uint8_t STARTUP_GAUGE_MAX_ATTEMPTS       = (uint8_t)CFG_STARTUP_GAUGE_MAX_ATTEMPTS;

const uint32_t GAUGE_FETCH_INTERVAL_MS       = (uint32_t)CFG_GAUGE_FETCH_INTERVAL_MS;
const uint32_t GAUGE_RETRY_INTERVAL_MS       = (uint32_t)CFG_GAUGE_RETRY_INTERVAL_MS;
const uint32_t FORECAST_FETCH_INTERVAL_MS    = (uint32_t)CFG_FORECAST_FETCH_INTERVAL_MS;
const uint32_t FORECAST_RETRY_INTERVAL_MS    = (uint32_t)CFG_FORECAST_RETRY_INTERVAL_MS;
const uint32_t MAIN_SCREEN_DURATION_MS       = (uint32_t)CFG_MAIN_SCREEN_DURATION_MS;
const uint32_t FORECAST_SCREEN_DURATION_MS   = (uint32_t)CFG_FORECAST_SCREEN_DURATION_MS;
const uint32_t INTERNAL_READ_INTERVAL_MS     = (uint32_t)CFG_INTERNAL_READ_INTERVAL_MS;
const uint32_t WIFI_RECONNECT_INTERVAL_MS    = (uint32_t)CFG_WIFI_RECONNECT_INTERVAL_MS;

const uint32_t TIME_SYNC_INTERVAL_MS         = (uint32_t)CFG_TIME_SYNC_INTERVAL_MS;
const uint32_t TIME_SYNC_RETRY_MS            = (uint32_t)CFG_TIME_SYNC_RETRY_MS;
const uint32_t DATE_CHECK_INTERVAL_MS        = (uint32_t)CFG_DATE_CHECK_INTERVAL_MS;

const float TEMP_DELTA = (float)CFG_TEMP_DELTA_C;
const float HUM_DELTA  = (float)CFG_HUM_DELTA_PCT;

/* -------------------------------------------------------------------------- */
/*                              Timekeeping                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Maintains a millis-based estimate of the current epoch.
 *
 * Synced periodically from system time (NTP) to avoid drift.
 */
struct TimeKeeper {
  bool valid           = false;  ///< true if baseEpoch is valid
  time_t baseEpoch     = 0;      ///< seconds since 1970
  uint32_t baseMillis  = 0;      ///< millis() at baseEpoch
  uint32_t lastSyncCheckMs = 0;  ///< throttle sync attempts
  uint32_t lastDateCheckMs = 0;  ///< throttle date checks
};

static TimeKeeper tk;

/**
 * @brief Check if a time_t value represents a valid date (post-2021).
 */
static bool isEpochValid(time_t t) {
  return t >= 1609459200;  // 2021-01-01
}

/**
 * @brief Sync the timekeeper from system time (NTP) if available.
 * @param nowMs Current millis() value.
 * @param force If true, ignore throttle interval.
 * @return true if timekeeper is now valid.
 */
static bool timekeeperSyncFromSystem(uint32_t nowMs, bool force) {
  if (!ntpConfigured) return false;

  const uint32_t intervalMs = tk.valid ? TIME_SYNC_INTERVAL_MS : TIME_SYNC_RETRY_MS;
  if (!force && (nowMs - tk.lastSyncCheckMs) < intervalMs) return tk.valid;
  tk.lastSyncCheckMs = nowMs;

  time_t t = time(nullptr);
  if (!isEpochValid(t)) return false;

  tk.baseEpoch = t;
  tk.baseMillis = nowMs;
  tk.valid = true;
  return true;
}

/**
 * @brief Get current local date as YYYY-MM-DD string.
 * @param[in]  nowMs  Current millis() value.
 * @param[out] out    Output buffer (at least 11 bytes).
 * @param[in]  outLen Size of output buffer.
 * @return true if date was written.
 */
static bool getLocalDateYYYYMMDD_fromTimekeeper(uint32_t nowMs, char* out, size_t outLen) {
  if (!out || outLen < 11) return false;
  if (!tk.valid) return false;

  const uint32_t deltaMs = nowMs - tk.baseMillis;
  time_t est = tk.baseEpoch + (time_t)(deltaMs / 1000UL);

  struct tm timeinfo;
  localtime_r(&est, &timeinfo);

  return snprintf(out, outLen, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday) > 0;
}

/* -------------------------------------------------------------------------- */
/*                                  Setup                                     */
/* -------------------------------------------------------------------------- */

void setup() {
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  LOG_I("Booting...");
  LOG_I("Reset reason: %s", ESP.getResetReason().c_str());

  initDisplay();
  LOG_I("Display initialized");

  initSensors();
  LOG_I("Sensors initialized");

  // Prime internal readings so the first screen draw doesn't show "no data" for ~2s.
  {
    float t, h;
    if (readInternalSensor(t, h)) {
      intTemperature = t;
      intHumidity    = h;
      haveIntData    = true;
    } else {
      haveIntData = false;
    }
  }

  const char* ssid = WIFI_SSID;
  const char* pass = WIFI_PASS;
  const bool haveWifiCreds = (ssid != nullptr) && (ssid[0] != '\0') && (std::strcmp(ssid, "your-ssid") != 0);
  wifiConfigured = haveWifiCreds;

  WiFi.mode(WIFI_STA);
  if (haveWifiCreds) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);
    lastWifiAttemptMs = millis();
    LOG_I("WiFi: connecting (non-blocking)");
  } else {
    LOG_W("WiFi: no credentials configured");
  }

  lastForecastFetchMs   = 0;
  lastForecastSuccessMs = 0;
  showMainScreen        = true;
  lastScreenSwitchMs    = millis();
  lastIntReadMs         = 0;
  currentScreenDuration = MAIN_SCREEN_DURATION_MS;
  needRedraw            = true;

  // External readings will be fetched once WiFi connects.
  startupGaugePending   = true;
  firstExtRedrawDone    = false;
  extDataRedrawPending  = false;
  startupGaugeAttempts  = 0;
  gaugeRetryPending     = true;

  // Reset timekeeper state.
  tk = {};
}

/* -------------------------------------------------------------------------- */
/*                                Main Loop                                   */
/* -------------------------------------------------------------------------- */

void loop() {
  uint32_t now = millis();

  // Configure NTP/TZ once WiFi connects.
  if (WiFi.status() == WL_CONNECTED && !ntpConfigured) {
    LOG_I("WiFi connected");
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
    setenv("TZ", TZ_INFO, 1);
    tzset();
    ntpConfigured = true;

    // Start syncing immediately after enabling NTP.
    (void)timekeeperSyncFromSystem(now, true);
  }

  // Maintain a millis-based epoch estimate; sync from system time occasionally.
  (void)timekeeperSyncFromSystem(now, false);

  // Keep internal sensor updated even if WiFi is down.
  if (now - lastIntReadMs >= INTERNAL_READ_INTERVAL_MS) {
    float t, h;
    lastIntReadMs = now;
    bool sensorOk = readInternalSensor(t, h);
    if (sensorOk) {
      bool updated   = (!haveIntData) || fabsf(t - intTemperature) >= TEMP_DELTA || fabsf(h - intHumidity) >= HUM_DELTA;
      intTemperature = t;
      intHumidity    = h;
      haveIntData    = true;
      (void)updated; // keep data updated, but do not redraw mid-screen
    } else if (haveIntData) {
      haveIntData = false;
    }
  }

  // Midnight rollover detection (local date based on NTP+TZ), throttled.
  if (tk.valid && (now - tk.lastDateCheckMs) >= DATE_CHECK_INTERVAL_MS) {
    tk.lastDateCheckMs = now;
    char d[11];
    if (getLocalDateYYYYMMDD_fromTimekeeper(now, d, sizeof(d))) {
      if (lastNtpDateValid && strcmp(d, lastNtpDate) != 0) {
        LOG_I("Midnight rollover: %s -> %s", lastNtpDate, d);
        midnightForecastPending = true;
      }
      strncpy(lastNtpDate, d, sizeof(lastNtpDate));
      lastNtpDate[sizeof(lastNtpDate) - 1] = '\0';
      lastNtpDateValid = true;
    }
  }

  // Attempt WiFi reconnect if credentials are configured.
  if (wifiConfigured && WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      LOG_W("WiFi disconnected, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      lastWifiAttemptMs = now;
    }
  }

  // Screen switching + drawing comes before any potentially slow network fetch.
  if (now - lastScreenSwitchMs >= currentScreenDuration) {
    showMainScreen        = !showMainScreen;
    lastScreenSwitchMs    = now;
    currentScreenDuration = showMainScreen ? MAIN_SCREEN_DURATION_MS : FORECAST_SCREEN_DURATION_MS;
    needRedraw            = true;

    // If external data arrived while we were on the forecast screen, redraw once on next main entry.
    if (showMainScreen && extDataRedrawPending && haveExtData && !firstExtRedrawDone) {
      needRedraw           = true;
      firstExtRedrawDone   = true;
      extDataRedrawPending = false;
    }
  }

  if (needRedraw) {
    if (showMainScreen) {
      drawMainScreen();
    } else {
      drawForecastScreen();
    }
    needRedraw = false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // One-time startup gauge fetch as soon as WiFi comes up.
    // This keeps boot fast (no blocking wait), but still populates the main screen quickly.
    if (startupGaugePending && !haveExtData && startupGaugeAttempts < STARTUP_GAUGE_MAX_ATTEMPTS) {
      if (startupGaugeAttempts == 0 || (now - lastGaugeAttemptMs) >= STARTUP_GAUGE_RETRY_INTERVAL_MS) {
        bool ok = fetchGaugeData();
        if (!ok) {
          // One quick retry; HTTPS can fail on first handshake after boot.
          delay(150);
          yield();
          ok = fetchGaugeData();
        }
        startupGaugeAttempts++;
        lastGaugeAttemptMs = now;

        if (ok) {
          lastGaugeSuccessMs = now;
          gaugeRetryPending  = false;

          // Ensure the main screen will update exactly once to replace "няма данни".
          if (!firstExtRedrawDone) {
            if (showMainScreen) {
              // We may have been blocked in this loop for several seconds while fetching.
              // Reset the screen timer so we don't immediately switch away before the redraw runs.
              lastScreenSwitchMs    = millis();
              currentScreenDuration = MAIN_SCREEN_DURATION_MS;
              needRedraw            = true;
              firstExtRedrawDone    = true;
            } else {
              extDataRedrawPending = true;
            }
          }

          startupGaugePending = false;
        } else {
          gaugeRetryPending = true;
        }
      }
    } else if (haveExtData) {
      startupGaugePending = false;
    }

    // Gauge fetch: retry more frequently after a failure (or at boot when no data).
    if (!haveExtData) gaugeRetryPending = true;
    uint32_t gaugeIntervalMs = (!gaugeRetryPending && haveExtData) ? GAUGE_FETCH_INTERVAL_MS : GAUGE_RETRY_INTERVAL_MS;
    if (!haveExtData || now - lastGaugeAttemptMs >= gaugeIntervalMs) {
      bool ok = fetchGaugeData();
      lastGaugeAttemptMs = now;
      if (ok) {
        lastGaugeSuccessMs = now;
        gaugeRetryPending  = false;

        // If we just got external data for the first time, schedule a single redraw.
        if (!firstExtRedrawDone && haveExtData) {
          if (showMainScreen) {
            lastScreenSwitchMs    = millis();
            currentScreenDuration = MAIN_SCREEN_DURATION_MS;
            needRedraw            = true;
            firstExtRedrawDone    = true;
          } else {
            extDataRedrawPending = true;
          }
        }
      } else {
        gaugeRetryPending = true;
      }
    }

    // Forecast fetch cadence:
    // - Normal: hourly (FORECAST_FETCH_INTERVAL_MS)
    // - If no forecast yet or after midnight rollover: retry interval (FORECAST_RETRY_INTERVAL_MS)
    uint32_t forecastIntervalMs = (!midnightForecastPending && forecastCount > 0) ? FORECAST_FETCH_INTERVAL_MS : FORECAST_RETRY_INTERVAL_MS;
    const bool firstForecastAttempt = (forecastCount == 0 && lastForecastFetchMs == 0);
    if (midnightForecastPending || firstForecastAttempt || (now - lastForecastFetchMs) >= forecastIntervalMs) {
      if (fetchForecast()) {
        lastForecastSuccessMs   = now;
        midnightForecastPending = false;
      }
      lastForecastFetchMs = now;
    }
  }

  // Log first time sync once it becomes valid (non-blocking).
  if (tk.valid && !lastNtpDateValid) {
    char d[11];
    if (getLocalDateYYYYMMDD_fromTimekeeper(now, d, sizeof(d))) {
      strncpy(lastNtpDate, d, sizeof(lastNtpDate));
      lastNtpDate[sizeof(lastNtpDate) - 1] = '\0';
      lastNtpDateValid = true;
      LOG_I("NTP synced, local date: %s", lastNtpDate);
    }
  }

  // Keep WiFi/OS responsive without a big blocking delay.
  delay(1);
  yield();
}
