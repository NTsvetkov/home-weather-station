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

#include "data.h"
#include "display.h"
#include "sensors.h"

unsigned long lastGaugeFetchMs      = 0;
unsigned long lastForecastFetchMs   = 0;
unsigned long lastForecastSuccessMs = 0;
unsigned long lastScreenSwitchMs    = 0;
unsigned long currentScreenDuration = 0;
unsigned long lastIntReadMs         = 0;
bool showMainScreen                 = true;
bool needRedraw                     = true;

static bool midnightForecastPending = false;
static String lastNtpDate = "";

/**
 * @brief Get current local date as YYYY-MM-DD (based on NTP time + TZ rules).
 * @return true if time is considered valid and out is set.
 */
static bool getLocalDateYYYYMMDD(String& out) {
  time_t t = time(nullptr);
  // Consider time valid only after a reasonable epoch (2021-01-01).
  if (t < 1609459200) return false;

  struct tm timeinfo;
  localtime_r(&t, &timeinfo);

  char buf[11];
  if (snprintf(buf, sizeof(buf), "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday) <= 0) {
    return false;
  }

  out = String(buf);
  return true;
}

const unsigned long GAUGE_FETCH_INTERVAL     = 180000;     // 3 min for live readings
const unsigned long FORECAST_FETCH_INTERVAL  = 3600000;    // 1 hour for forecast
const unsigned long FORECAST_RETRY_INTERVAL  = 300000;     // 5 min retry when missing
const unsigned long MAIN_SCREEN_DURATION     = 10000;      // 10s
const unsigned long FORECAST_SCREEN_DURATION = 10000;      // 10s
const unsigned long INTERNAL_READ_INTERVAL   = 2000;       // read AHT20 every 2s

const float TEMP_DELTA = 0.2f;                             // trigger redraw if temp changes >=0.2C
const float HUM_DELTA  = 1.0f;                             // trigger redraw if humidity changes >=1%

void setup() {
  Serial.begin(115200);
  Serial.println("ILI9341 and AHT20 Test!");

  const char* ssid = WIFI_SSID;
  const char* pass = WIFI_PASS;
  const bool haveWifiCreds = (ssid != nullptr) && (ssid[0] != '\0') && (std::strcmp(ssid, "your-ssid") != 0);

  WiFi.mode(WIFI_STA);
  if (haveWifiCreds) {
    WiFi.begin(ssid, pass);

    Serial.print("Свързване към WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(" свързано!");

    // NTP time sync (used to refresh forecast exactly at local midnight).
    // Keep NTP in UTC and apply TZ rules locally.
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
    setenv("TZ", TZ_INFO, 1);
    tzset();

    // Wait briefly for initial time sync (non-blocking-ish with yield)
    const unsigned long startWait = millis();
    while (millis() - startWait < 5000) {
      String d;
      if (getLocalDateYYYYMMDD(d)) {
        lastNtpDate = d;
        Serial.print("NTP time synced, local date: ");
        Serial.println(lastNtpDate);
        break;
      }
      delay(100);
      yield();
    }
  } else {
    Serial.println("WiFi: няма конфигурация (config.h липсва или е примерен) -> пропускам свързване");
  }

  initDisplay();
  initSensors();

  lastGaugeFetchMs      = 0;
  lastForecastFetchMs   = 0;
  lastForecastSuccessMs = 0;
  lastScreenSwitchMs    = millis();
  lastIntReadMs         = 0;
  currentScreenDuration = MAIN_SCREEN_DURATION;
  needRedraw            = true;
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    // Detect midnight based on NTP/localtime (independent of gauge refresh schedule).
    {
      String d;
      if (getLocalDateYYYYMMDD(d)) {
        if (lastNtpDate.length() > 0 && d != lastNtpDate) {
          Serial.print("Midnight rollover detected (NTP): ");
          Serial.print(lastNtpDate);
          Serial.print(" -> ");
          Serial.println(d);
          midnightForecastPending = true;
        }
        lastNtpDate = d;
      }
    }

    // Read internal sensor even if gauge fetch fails (rate-limit + change threshold).
    if (now - lastIntReadMs >= INTERNAL_READ_INTERVAL) {
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

    if (!haveExtData || now - lastGaugeFetchMs >= GAUGE_FETCH_INTERVAL) {
      Serial.println("Gauge fetch attempt");
      fetchGaugeData();
      lastGaugeFetchMs = now;
    }

    // If we detected a new day (midnight), keep trying more frequently until forecast refresh succeeds.
    unsigned long forecastInterval = (!midnightForecastPending && forecastCount > 0) ? FORECAST_FETCH_INTERVAL : FORECAST_RETRY_INTERVAL;
    if (midnightForecastPending || forecastCount == 0 || now - lastForecastFetchMs >= forecastInterval) {
      Serial.println("Forecast fetch attempt");
      if (fetchForecast()) {
        lastForecastSuccessMs   = now;
        midnightForecastPending = false;
      }
      lastForecastFetchMs = now;
    }
  }

  if (now - lastScreenSwitchMs >= currentScreenDuration) {
    showMainScreen        = !showMainScreen;
    lastScreenSwitchMs    = now;
    currentScreenDuration = showMainScreen ? MAIN_SCREEN_DURATION : FORECAST_SCREEN_DURATION;
    needRedraw            = true;
  }

  if (needRedraw) {
    if (showMainScreen) {
      drawMainScreen();
    } else {
      drawForecastScreen();
    }
    needRedraw = false;
  }

  delay(500); // slow down loop; external data is minutes apart and internal changes slowly
}