# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP8266 (NodeMCU v3) weather station displaying indoor/outdoor temperature, humidity, pressure, and 3-day forecast on a 2.4" ILI9341 TFT display. Indoor readings come from an AHT20 I2C sensor; outdoor data is fetched via HTTPS from a meter.ac gauge; forecast comes from the Open-Meteo API.

## Build Commands

```bash
pio run                  # Build only
pio run -t upload        # Build and flash to device
pio device monitor       # Serial monitor at 115200 baud
```

First-time setup: `cp src/config.example.h src/config.h` and edit WiFi credentials. `config.h` is gitignored.

CI runs `pio run` on every PR and push to develop/main (`.github/workflows/ci.yml`). There are no unit tests.

## Architecture

Six modules in `src/`:

| Module | Role |
|--------|------|
| `main.cpp` | Event loop, state machine, NTP timekeeping (`TimeKeeper` struct), WiFi lifecycle, screen switching |
| `data.cpp/h` | HTTPS fetching (gauge CSV + Open-Meteo JSON), trend calculation via ring buffer |
| `display.cpp/h` | TFT rendering: `drawMainScreen()`, `drawForecastScreen()`, `pickDayIcon()` |
| `sensors.cpp/h` | AHT20 I2C reads: `initSensors()`, `readInternalSensor()` |
| `utils.cpp/h` | Graphics helpers: text alignment, trend indicators, weather icons, UTF-8→Cyrillic conversion |
| `config.h` | User secrets + all tunable intervals (gitignored; template in `config.example.h`) |
| `debug.h` | `LOG_I/LOG_W/LOG_E` macros storing format strings in PROGMEM; verbosity via `DEBUG_LEVEL` 0-3 |

**Data flow:** AHT20 → `intTemperature/intHumidity` (every 2s) · meter.ac → `extTemperature/extHumidity/extPressure` + trends (every 3min) · Open-Meteo → `forecast[3]` (every 1hr, also on midnight rollover) · Display rotates main / daily / main / hourly screens via `screenCycleIndex`; `needRedraw = true` triggers redraws.

**Scheduling:** Timers are millis-based, but HTTP and sensor reads are synchronous. At most one HTTP request runs per loop. Each endpoint has its own retry backoff and update time. `delay(1)` + `yield()` feed the watchdog. HTTPS waits for valid NTP time.

## Key Patterns

**Configuration:** All tunables use `#ifndef` guards with defaults. Override by defining `CFG_*` macros in `config.h`. Add new options to `config.example.h` first, then use `#ifndef` in the consuming `.cpp`.

**Trend calculation:** Ring buffer of samples in `data.cpp`. `findRefSample()` looks back `TREND_WINDOW_MINUTES`, `calcTrend()` returns -1/0/+1 with per-metric thresholds (temp ≥ 0.3°C, etc.).

**Forecast icons:** `pickDayIcon()` uses priority-based selection: storms (WMO 95/96/99) → heavy precip → light precip → cloud cover thresholds → defaults. Snow vs rain determined by tMax ≤ 2°C.

**Timekeeping:** `TimeKeeper` syncs from NTP, then tracks time via millis offset. Midnight detection refreshes the daily/hourly schedules independently and hides yesterday's hourly blocks.

## ESP8266 Constraints

- ~80KB usable RAM. Use PROGMEM for strings, stack buffers (`char buf[N]`) instead of `String` in loops.
- BearSSL verifies HTTPS with ISRG Root X1/X2 from `tls_roots.h`. Never use `setInsecure()`. MFLN is probed before selecting a 2048-byte RX buffer; otherwise use 16384 bytes.
- Must call `yield()` or `delay(1)` in any long-running loop to prevent watchdog resets.
- Default JSON capacities: daily 2048 bytes (four API days), hourly 4000 bytes. User configurations may override them. Body readers cap both size and elapsed time.
- A pre-build AHTX0 patch bounds busy waits at one second and fails the build if the pinned upstream implementation changes.

## Adding Features

**New sensor:** Add function in `sensors.cpp`, declare in `sensors.h`, add state variable in `data.h`, call from `main.cpp` loop.

**New display element:** Add drawing function in `display.cpp` following `formatTempC1()` pattern, call from `drawMainScreen()`/`drawForecastScreen()`, use `utils.h` alignment helpers.

**New config option:** Add `#define CFG_*` with default in `config.example.h`, add `#ifndef` guard in the consuming `.cpp`.

## Branching

- `main` — stable releases
- `develop` — active development (default branch for PRs)
