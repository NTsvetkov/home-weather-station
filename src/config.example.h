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
// After this long without WiFi the status bar shows "НЯМА WIFI" instead of "СВЪРЗВАНЕ".
#define CFG_WIFI_DOWN_ALERT_MS             60000UL

// Status bar: age of the last update (seconds) at which its time turns amber / red.
// Past the "old" limit on the main screen the outdoor values are greyed out.
#define CFG_GAUGE_STALE_WARN_S             600UL
#define CFG_GAUGE_STALE_OLD_S              1800UL
#define CFG_FORECAST_STALE_WARN_S          5400UL
#define CFG_FORECAST_STALE_OLD_S           10800UL

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

// TLS RX is negotiated automatically: 2048 with MFLN, otherwise 16384.
// Legacy CFG_TLS_*_BUFFER_BYTES settings are ignored to avoid unsafe small records.
#define CFG_HTTP_TOTAL_TIMEOUT_MS          20000UL
#define CFG_HTTP_MAX_HEADER_BYTES          4096
#define CFG_GAUGE_MAX_RESPONSE_BYTES        512
#define CFG_FORECAST_MAX_RESPONSE_BYTES     8192
#define CFG_HOURLY_MAX_RESPONSE_BYTES       12288
#define CFG_SENSOR_REINIT_INTERVAL_MS      30000UL

// ArduinoJson document capacity
#define CFG_FORECAST_JSON_DOC_CAPACITY     2048

// Today screen duration
#define CFG_TODAY_SCREEN_DURATION_MS       10000UL

// ArduinoJson document capacity for hourly forecast
#define CFG_HOURLY_JSON_DOC_CAPACITY       4000

// Endpoints
#define CFG_GAUGE_URL    "https://meter.ac/gs/nodes/N200/gauge.txt"
#define CFG_FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=42.1859191&longitude=24.3398302&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,wind_speed_10m_max,cloud_cover_mean&forecast_days=4&models=ecmwf_ifs&timezone=auto"
#define CFG_HOURLY_FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=42.1859191&longitude=24.3398302&hourly=temperature_2m,precipitation,weather_code,cloud_cover,wind_speed_10m,is_day&forecast_days=1&models=ecmwf_ifs&timezone=auto"
