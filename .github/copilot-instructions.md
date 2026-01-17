# Copilot Instructions for Weather Station Project

## Architecture Overview
ESP8266-based weather station with modular source structure:
- **`main.cpp`** - Application loop, state machine, timekeeping (NTP), WiFi lifecycle
- **`data.cpp/h`** - External data: HTTP fetching (meter.ac gauge, Open-Meteo forecast), trend calculation via ring buffer
- **`display.cpp/h`** - TFT rendering: `drawMainScreen()`, `drawForecastScreen()`, icon selection logic
- **`sensors.cpp/h`** - AHT20 I2C sensor: `initSensors()`, `readInternalSensor()`
- **`utils.cpp/h`** - GFX helpers: `drawCenteredText()`, `drawTrendIndicator()`
- **`config.h`** - User secrets + tunable intervals (gitignored; copy from `config.example.h`)

## Data Flow
1. **Internal** → `readInternalSensor()` polls AHT20 every 2s → `intTemperature/intHumidity`
2. **External** → `fetchGaugeData()` from meter.ac → `extTemperature/extHumidity/extPressure` + trend history
3. **Forecast** → `fetchForecast()` from Open-Meteo → `forecast[3]` array with processed daily data
4. **Display** → Main loop alternates screens via `showMainScreen` flag; `needRedraw` triggers redraws

## Developer Workflows
```bash
pio run -t upload        # Build and flash
pio device monitor       # Serial at 115200 baud
cp src/config.example.h src/config.h  # First-time setup (edit with WiFi creds)
```

## Critical Patterns

### Configuration via Macros
All tunables use `#ifndef` guards in `main.cpp` with fallbacks. Override in `config.h`:
```cpp
#define CFG_GAUGE_FETCH_INTERVAL_MS 180000UL  // 3 min outdoor refresh
#define TREND_WINDOW_MINUTES 30               // History window for trends
```

### Debug Logging
Use `LOG_I/LOG_W/LOG_E` from `debug.h` (stores strings in PROGMEM). Control via `DEBUG_LEVEL` (0-3).

### ESP8266 Constraints
- Call `yield()` or `delay(1)` in loops to prevent watchdog resets
- Use `WiFiClientSecureBearSSL` with `setInsecure()` for HTTPS (no cert validation)
- Avoid `String` class in loops; use stack buffers (`char buf[32]`)

### Trend Calculation
Ring buffer in `data.cpp` stores 8 samples. `findRefSample()` looks back `TREND_WINDOW_MINUTES` to compute direction (+1/0/-1) via `calcTrend()` with configurable thresholds.

### Timekeeping (NTP)
The `TimeKeeper` struct in `main.cpp` maintains a millis-based epoch estimate synced from NTP:
- **Sync cadence**: `CFG_TIME_SYNC_INTERVAL_MS` (1h default), or `CFG_TIME_SYNC_RETRY_MS` (5s) while NTP invalid
- **Midnight rollover**: `getLocalDateYYYYMMDD_fromTimekeeper()` checks date every `CFG_DATE_CHECK_INTERVAL_MS`; when date changes, `midnightForecastPending = true` triggers a forecast refresh
- **Timezone**: Configured via POSIX `TZ_INFO` string (default: Bulgaria EET/EEST)

```cpp
// TimeKeeper fields
bool valid;          // true once NTP synced
time_t baseEpoch;    // seconds since 1970
uint32_t baseMillis; // millis() at sync time
```

### Forecast Icon Logic
`pickDayIcon()` in `display.cpp` selects icons based on multiple weather parameters (not just WMO codes):

| Priority | Condition | Icon |
|----------|-----------|------|
| 1 | WMO 95/96/99 | `ICON_STORM` |
| 2 | precip ≥ 10mm | `ICON_SNOW` (tMax ≤ 2°C) or `ICON_HEAVY_RAIN` |
| 3 | precip ≥ 0.2mm | `ICON_SNOW` (tMax ≤ 2°C) or `ICON_RAIN` |
| 4 | cloudMean < 25% | `ICON_SUN` |
| 5 | cloudMean < 70% | `ICON_PARTLY` |
| 6 | default | `ICON_CLOUDY` |

Wind is displayed as a label, not an icon. The `DayIcon` enum maps to drawing functions in the forecast screen.

### Display Updates
Set `needRedraw = true` to trigger screen refresh. Screen switching uses `lastScreenSwitchMs` timer. Internal sensor updates don't force redraws unless delta exceeds `CFG_TEMP_DELTA_C`.

## Adding Features

**New sensor reading:**
1. Add function to `sensors.cpp`, declare in `sensors.h`
2. Add state variable to `data.h` (e.g., `extern float intPressure;`)
3. Update `main.cpp` loop to call and store value

**New display element:**
1. Add drawing function to `display.cpp` (follow `formatTempC1()` pattern for formatting)
2. Call from `drawMainScreen()` or `drawForecastScreen()`
3. Use `utils.h` helpers for alignment (`drawCenteredText`, `drawRightAlignedText`)

**New config option:**
1. Add `#define CFG_*` to `config.example.h` with default
2. Add `#ifndef` guard in the consuming `.cpp` file
