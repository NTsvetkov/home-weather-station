#pragma once

// Copy this file to `config.h` and add `config.h` to your `.gitignore`.
// Put all personal/site-specific settings here.

// WiFi credentials
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// Trend window for outdoor readings (minutes).
// Recommended: 30 (stable) or 10 (more reactive).
#define TREND_WINDOW_MINUTES 30

// Timezone (POSIX TZ string) used for NTP local-midnight detection.
// Bulgaria default: EET (UTC+2) with EEST (UTC+3) DST.
#define TZ_INFO "EET-2EEST,M3.5.0/3,M10.5.0/4"

// ----------------------------
// Tuning knobs (current values)
// ----------------------------

// UI / scheduling
#define CFG_MAIN_SCREEN_DURATION_MS        10000UL
#define CFG_FORECAST_SCREEN_DURATION_MS    10000UL

// Internal sensor
#define CFG_INTERNAL_READ_INTERVAL_MS      2000UL

// Redraw thresholds
#define CFG_TEMP_DELTA_C                   0.2f
#define CFG_HUM_DELTA_PCT                  1.0f

// WiFi
#define CFG_WIFI_RECONNECT_INTERVAL_MS     15000UL

// Timekeeping
// How often to resync the millis-based clock from NTP time.
#define CFG_TIME_SYNC_INTERVAL_MS          3600000UL
// While NTP isn't valid after boot, how often to retry grabbing an epoch base.
#define CFG_TIME_SYNC_RETRY_MS             5000UL
// How often to check for date change (midnight rollover).
#define CFG_DATE_CHECK_INTERVAL_MS         60000UL

// Startup gauge behavior
#define CFG_STARTUP_GAUGE_RETRY_INTERVAL_MS 2000UL
#define CFG_STARTUP_GAUGE_MAX_ATTEMPTS      5

// Gauge fetch cadence
#define CFG_GAUGE_FETCH_INTERVAL_MS        180000UL
#define CFG_GAUGE_RETRY_INTERVAL_MS        30000UL

// Forecast fetch cadence
#define CFG_FORECAST_FETCH_INTERVAL_MS     3600000UL
#define CFG_FORECAST_RETRY_INTERVAL_MS     300000UL

// Networking / parsing
#define CFG_GAUGE_HTTP_TIMEOUT_MS          3500UL
#define CFG_FORECAST_HTTP_TIMEOUT_MS       9000UL

// TLS buffers
#define CFG_TLS_RX_BUFFER_BYTES_GAUGE      1024
#define CFG_TLS_TX_BUFFER_BYTES_GAUGE      512
#define CFG_TLS_RX_BUFFER_BYTES_FORECAST   2048
#define CFG_TLS_TX_BUFFER_BYTES_FORECAST   512

// ArduinoJson document capacity
#define CFG_FORECAST_JSON_DOC_CAPACITY     8000

// Endpoints
#define CFG_GAUGE_URL    "https://meter.ac/gs/nodes/N200/gauge.txt"
#define CFG_FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=42.1859191&longitude=24.3398302&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,wind_speed_10m_max,cloud_cover_mean&models=ecmwf_ifs&timezone=auto"
