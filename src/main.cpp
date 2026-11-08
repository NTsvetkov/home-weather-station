#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <math.h>
#include <cstring>
#include <time.h>

#include "project_config.h"

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
#ifndef CFG_TODAY_SCREEN_DURATION_MS
#define CFG_TODAY_SCREEN_DURATION_MS 10000UL
#endif
#ifndef CFG_INTERNAL_READ_INTERVAL_MS
#define CFG_INTERNAL_READ_INTERVAL_MS 2000UL
#endif
#ifndef CFG_WIFI_RECONNECT_INTERVAL_MS
#define CFG_WIFI_RECONNECT_INTERVAL_MS 15000UL
#endif
// After this long without WiFi the status bar shows "НЯМА WIFI" instead of "СВЪРЗВАНЕ".
#ifndef CFG_WIFI_DOWN_ALERT_MS
#define CFG_WIFI_DOWN_ALERT_MS 60000UL
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

// All retries use the completion time, including failed requests at boot/midnight.
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
  void finished(bool ok) {
    attempted = true;
    lastAttemptMs = millis();
    if (ok) {
      haveSuccess = true;
      lastSuccessMs = lastAttemptMs;
      failures = 0;
    } else if (failures < 255) {
      ++failures;
    }
  }
  void refresh() { attempted = false; failures = 0; }
};
static FetchSchedule gaugeSchedule, dailySchedule, hourlySchedule;
uint32_t lastScreenSwitchMs         = 0;
uint32_t currentScreenDuration      = 0;
uint32_t lastIntReadMs              = 0;
/**
 * @brief Screen cycle: 0=MAIN, 1=FORECAST, 2=MAIN, 3=TODAY.
 * Repeats: main → 3day → main → today → main → ...
 */
uint8_t screenCycleIndex            = 0;
bool needRedraw                     = true;

static char lastNtpDate[11]         = {0};
static bool lastNtpDateValid        = false;

static bool wifiConfigured          = false;
static uint32_t lastWifiAttemptMs   = 0;
static bool ntpConfigured           = false;
static bool firstExtRedrawDone      = false;
static bool extDataRedrawPending    = false;
static uint8_t startupGaugeAttempts = 0;
static bool wifiDownTimerRunning    = false;
static uint32_t wifiDownSinceMs     = 0;

const uint32_t STARTUP_GAUGE_RETRY_INTERVAL_MS = (uint32_t)CFG_STARTUP_GAUGE_RETRY_INTERVAL_MS;
const uint8_t STARTUP_GAUGE_MAX_ATTEMPTS       = (uint8_t)CFG_STARTUP_GAUGE_MAX_ATTEMPTS;

const uint32_t GAUGE_FETCH_INTERVAL_MS       = (uint32_t)CFG_GAUGE_FETCH_INTERVAL_MS;
const uint32_t GAUGE_RETRY_INTERVAL_MS       = (uint32_t)CFG_GAUGE_RETRY_INTERVAL_MS;
const uint32_t FORECAST_FETCH_INTERVAL_MS    = (uint32_t)CFG_FORECAST_FETCH_INTERVAL_MS;
const uint32_t FORECAST_RETRY_INTERVAL_MS    = (uint32_t)CFG_FORECAST_RETRY_INTERVAL_MS;
const uint32_t MAIN_SCREEN_DURATION_MS       = (uint32_t)CFG_MAIN_SCREEN_DURATION_MS;
const uint32_t FORECAST_SCREEN_DURATION_MS   = (uint32_t)CFG_FORECAST_SCREEN_DURATION_MS;
const uint32_t TODAY_SCREEN_DURATION_MS      = (uint32_t)CFG_TODAY_SCREEN_DURATION_MS;
const uint32_t INTERNAL_READ_INTERVAL_MS     = (uint32_t)CFG_INTERNAL_READ_INTERVAL_MS;
const uint32_t WIFI_RECONNECT_INTERVAL_MS    = (uint32_t)CFG_WIFI_RECONNECT_INTERVAL_MS;
const uint32_t WIFI_DOWN_ALERT_MS            = (uint32_t)CFG_WIFI_DOWN_ALERT_MS;

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
  bool valid               = false;  ///< true if baseEpoch is valid
  time_t baseEpoch         = 0;      ///< seconds since 1970
  uint32_t baseMillis      = 0;      ///< millis() at baseEpoch
  uint32_t lastSyncCheckMs = 0;      ///< throttle sync attempts
  uint32_t lastDateCheckMs = 0;      ///< throttle date checks
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

  tk.baseEpoch  = t;
  tk.baseMillis = nowMs;
  tk.valid      = true;
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

/**
 * @brief Current epoch estimate from the timekeeper (valid only when tk.valid).
 */
static time_t timekeeperNow(uint32_t nowMs) {
  return tk.baseEpoch + (time_t)((nowMs - tk.baseMillis) / 1000UL);
}

/**
 * @brief Collect WiFi / clock / last-update info for the status bar.
 * @param screen Current screen cycle index (separate daily/hourly update times).
 */
static UiStatus buildUiStatus(uint32_t nowMs, uint8_t screen) {
  UiStatus st;

  if (!wifiConfigured) {
    st.wifi = WIFI_UI_DOWN;
  } else if (WiFi.status() == WL_CONNECTED) {
    st.wifi = WIFI_UI_OK;
    st.rssi = WiFi.RSSI();
  } else if (wifiDownTimerRunning && (nowMs - wifiDownSinceMs) >= WIFI_DOWN_ALERT_MS) {
    st.wifi = WIFI_UI_DOWN;
  } else {
    st.wifi = WIFI_UI_CONNECTING;
  }

  time_t nowEpoch = 0;
  if (tk.valid) {
    nowEpoch = timekeeperNow(nowMs);
    struct tm ti;
    localtime_r(&nowEpoch, &ti);
    st.clockValid = true;
    st.hour       = (uint8_t)ti.tm_hour;
  }

  const FetchSchedule& schedule = screen == 1 ? dailySchedule : (screen == 3 ? hourlySchedule : gaugeSchedule);
  const bool have = screen == 1 ? forecastCount > 0 : (screen == 3 ? haveTodayForecast : haveExtData);
  if (have && schedule.haveSuccess) {
    st.haveUpdate = true;
    st.updAgeS = (nowMs - schedule.lastSuccessMs) / 1000UL;
    if (screen != 1 && screen != 3 && tk.valid && gaugeObservationEpoch) {
      st.updAgeS = nowEpoch > gaugeObservationEpoch ? (uint32_t)(nowEpoch - gaugeObservationEpoch) : 0;
    }
    if (tk.valid) {
      time_t updEpoch = nowEpoch - (time_t)st.updAgeS;
      struct tm ti;
      localtime_r(&updEpoch, &ti);
      st.updTimeValid = true;
      st.updHour      = (uint8_t)ti.tm_hour;
      st.updMin       = (uint8_t)ti.tm_min;
    }
  }

  return st;
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

  if (initSensors()) LOG_I("Sensors initialized");

  // Prime internal readings so the first screen draw doesn't show "no data" for ~2s.
  {
    float t, h;
    if (readInternalSensor(t, h)) {
      intTemperature = t;
      intHumidity    = h;
      haveIntData    = true;
      LOG_I("Indoor: %.1f C, %.0f%%", t, h);
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

  gaugeSchedule = {};
  dailySchedule = {};
  hourlySchedule = {};
  screenCycleIndex      = 0;
  lastScreenSwitchMs    = millis();
  lastIntReadMs         = 0;
  currentScreenDuration = MAIN_SCREEN_DURATION_MS;
  needRedraw            = true;

  // External readings will be fetched once WiFi connects.
  firstExtRedrawDone    = false;
  extDataRedrawPending  = false;
  startupGaugeAttempts  = 0;

  // Reset timekeeper state.
  tk = {};
}

/**
 * @brief Reset screen timer to show main screen immediately.
 * @param nowMs Current millis() value.
 */
static inline void resetToMainScreen(uint32_t nowMs) {
  screenCycleIndex      = 0;
  lastScreenSwitchMs    = nowMs;
  currentScreenDuration = MAIN_SCREEN_DURATION_MS;
  needRedraw            = true;
}

/**
 * @brief Get screen duration for the given cycle index.
 */
static inline uint32_t durationForCycleIndex(uint8_t idx) {
  switch (idx) {
    case 1: return FORECAST_SCREEN_DURATION_MS;
    case 3: return TODAY_SCREEN_DURATION_MS;
    default: return MAIN_SCREEN_DURATION_MS; // 0 and 2 are main
  }
}

/** @brief Check if current cycle index is a main screen. */
static inline bool isMainScreen() {
  return (screenCycleIndex == 0 || screenCycleIndex == 2);
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
      if (lastNtpDateValid && memcmp(d, lastNtpDate, 10) != 0) {
        LOG_I("Midnight rollover: %s -> %s", lastNtpDate, d);
        dailySchedule.refresh();
        hourlySchedule.refresh();
        // Yesterday's blocks must never be displayed under today's heading.
        if (haveTodayForecast && strcmp(todayForecastDate, d) != 0) {
          haveTodayForecast = false;
          if (screenCycleIndex == 3) needRedraw = true;
        }
      }
      strncpy(lastNtpDate, d, sizeof(lastNtpDate));
      lastNtpDate[sizeof(lastNtpDate) - 1] = '\0';
      lastNtpDateValid = true;
    }
  }

  // Track how long WiFi has been down (for the status bar).
  if (WiFi.status() == WL_CONNECTED) {
    wifiDownTimerRunning = false;
  } else if (!wifiDownTimerRunning) {
    wifiDownTimerRunning = true;
    wifiDownSinceMs      = now;
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
    screenCycleIndex      = (screenCycleIndex + 1) % 4; // 0→1→2→3→0...
    lastScreenSwitchMs    = now;
    currentScreenDuration = durationForCycleIndex(screenCycleIndex);
    needRedraw            = true;

    // If external data arrived while we were on a non-main screen, redraw once on next main entry.
    if (isMainScreen() && extDataRedrawPending && haveExtData && !firstExtRedrawDone) {
      needRedraw           = true;
      firstExtRedrawDone   = true;
      extDataRedrawPending = false;
    }
  }

  if (needRedraw) {
    const UiStatus st = buildUiStatus(now, screenCycleIndex);
    switch (screenCycleIndex) {
      case 1: drawForecastScreen(st); break;
      case 3: drawTodayScreen(st);    break;
      default: drawMainScreen(st);    break; // 0 and 2
    }
    needRedraw = false;
  }

  // At most one blocking request per loop; sensors and display run between them.
  // Certificate validation requires a valid clock, so boot waits for NTP.
  if (WiFi.status() == WL_CONNECTED && tk.valid) {
    const uint32_t retry = !gaugeSchedule.haveSuccess && startupGaugeAttempts < STARTUP_GAUGE_MAX_ATTEMPTS
        ? STARTUP_GAUGE_RETRY_INTERVAL_MS : GAUGE_RETRY_INTERVAL_MS;
    if (gaugeSchedule.due(now, GAUGE_FETCH_INTERVAL_MS, retry)) {
      const bool ok = fetchGaugeData();
      gaugeSchedule.finished(ok);
      if (startupGaugeAttempts < STARTUP_GAUGE_MAX_ATTEMPTS) ++startupGaugeAttempts;
      if (ok && !firstExtRedrawDone) {
        if (isMainScreen()) {
          resetToMainScreen(millis());
          firstExtRedrawDone = true;
        } else {
          extDataRedrawPending = true;
        }
      }
    } else if (dailySchedule.due(now, FORECAST_FETCH_INTERVAL_MS, FORECAST_RETRY_INTERVAL_MS)) {
      dailySchedule.finished(fetchForecast());
    } else if (hourlySchedule.due(now, FORECAST_FETCH_INTERVAL_MS, FORECAST_RETRY_INTERVAL_MS)) {
      hourlySchedule.finished(fetchHourlyForecast());
    }
    now = millis();
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
