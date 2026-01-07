#pragma once

// Copy this file to `config.h` and add `config.h` to your `.gitignore`.
// Put all personal/site-specific settings here.

// WiFi credentials
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// Trend window for outdoor readings (minutes).
// Recommended: 30 (stable) or 10 (more reactive).
#define TREND_WINDOW_MINUTES 10

// Timezone (POSIX TZ string) used for NTP local-midnight detection.
// Bulgaria default: EET (UTC+2) with EEST (UTC+3) DST.
#define TZ_INFO "EET-2EEST,M3.5.0/3,M10.5.0/4"
