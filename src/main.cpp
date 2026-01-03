
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <math.h>
#include "config.h"
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Свързване към WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" свързано!");

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
    // Safety: read internal sensor even if gauge fetch fails, but rate-limit and add change threshold
    if (now - lastIntReadMs >= INTERNAL_READ_INTERVAL) {
      float t, h;
      lastIntReadMs = now;
      bool sensorOk = readInternalSensor(t, h);
      if (sensorOk) {
        bool updated   = (!haveIntData) || fabsf(t - intTemperature) >= TEMP_DELTA || fabsf(h - intHumidity) >= HUM_DELTA;
        intTemperature = t;
        intHumidity    = h;
        haveIntData    = true;
        if (updated) needRedraw = true;
      } else if (haveIntData) {
        haveIntData = false;
        needRedraw  = true;
      }
    }

    if (!haveExtData || now - lastGaugeFetchMs >= GAUGE_FETCH_INTERVAL) {
      Serial.println("Gauge fetch attempt");
      if (fetchGaugeData()) needRedraw = true;
      lastGaugeFetchMs = now;
    }

    unsigned long forecastInterval = (forecastCount > 0) ? FORECAST_FETCH_INTERVAL : FORECAST_RETRY_INTERVAL;
    if (forecastCount == 0 || now - lastForecastFetchMs >= forecastInterval) {
      Serial.println("Forecast fetch attempt");
      if (fetchForecast()) {
        lastForecastSuccessMs = now;
        needRedraw            = true;
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