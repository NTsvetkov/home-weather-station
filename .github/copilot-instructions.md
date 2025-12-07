# Copilot Instructions for Weather Station Project

## Project Overview
- This is an ESP8266-based weather station project using PlatformIO.
- The main application is in `src/main.cpp` and uses the TFT_eSPI library for display, Adafruit_AHTX0 for sensor input, and WiFi/HTTP libraries for network communication.
- The project is configured via `platformio.ini`.

## Architecture & Data Flow
- The device connects to WiFi, fetches weather data from a remote endpoint, and displays both external (fetched) and internal (sensor) temperature, humidity, and pressure on a TFT display.
- Display logic is modularized into functions: `drawThermometer`, `drawHygrometer`, `drawPressureBar`, `drawStar`, and `numberBox`.
- The main loop alternates between fetching/displaying weather data and running a sprite demo (for testing the display).

## Developer Workflows
- **Build & Upload:** Use PlatformIO commands (`pio run`, `pio upload`) or the PlatformIO VS Code extension.
- **Serial Monitor:** Use `pio device monitor` or the VS Code serial monitor for debug output at 115200 baud.
- **Dependencies:** Libraries are included via `#include` in `main.cpp` and must be installed via PlatformIO's library manager if not present.

## Project-Specific Patterns
- All display drawing is abstracted into helper functions for clarity and reuse.
- WiFi credentials are hardcoded in `main.cpp` for demo purposes; consider secrets management for production.
- The sprite demo (star/numberBox) is used for display testing and can be toggled by replacing the main loop logic.
- Comments in code are often bilingual (English/Bulgarian) for clarity to local developers.

## Key Files & Directories
- `src/main.cpp`: Main application logic, display, and sensor code.
- `lib/TFT_eSPI/User_Setups/Setup_ILI9341_ESP8266.h`: Display hardware configuration.
- `platformio.ini`: PlatformIO project configuration.

## Integration Points
- External weather data is fetched from `https://meter.ac/gs/nodes/N200/gauge.txt`.
- Internal sensor data is read from an AHT20 sensor via I2C.

## Example: Adding a New Display Feature
- Add a new drawing function in `main.cpp` (e.g., `void drawWindGauge(...)`).
- Call the function from the main loop after clearing or updating the display.

## Conventions
- Use `TFT_eSprite` for off-screen drawing and transparency effects.
- Use `yield()` in long loops to prevent ESP8266 watchdog resets.
- Use `delay(60000)` at the end of the loop to update once per minute.

---
For questions about hardware setup, see `lib/TFT_eSPI/User_Setups/Setup_ILI9341_ESP8266.h` and code comments in `main.cpp`.
