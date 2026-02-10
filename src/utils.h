#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>

/**
 * @file utils.h
 * @brief Small helpers shared by the UI.
 */

/**
 * @brief Icon bucket for forecast rendering.
 */
enum DayIcon {
  ICON_SUN,
  ICON_PARTLY,
  ICON_CLOUDY,
  ICON_RAIN,
  ICON_HEAVY_RAIN,
  ICON_SNOW,
  ICON_STORM
};

/** @brief Draw C-string text centered around centerX using the provided gfx instance. */
void drawCenteredText(Adafruit_GFX& gfx, const char* text, int centerX, int y, uint8_t size, uint16_t color);

/** @brief Draw C-string text right-aligned to rightX using the provided gfx instance. */
void drawRightAlignedText(Adafruit_GFX& gfx, const char* text, int rightX, int y, uint8_t size, uint16_t color);

/** @brief Small triangle indicator for trends (-1/0/+1). */
void drawTrendIndicator(Adafruit_GFX& gfx, int x, int y, int8_t trend);

/**
 * @brief Draw a pressure bar with a label and trend indicator.
 * @param pressure Pressure in hPa.
 * @param trend Trend value (-1/0/+1).
 */
void drawPressureBar(Adafruit_GFX& gfx, int x, int y, float pressure, int8_t trend);

/**
 * @brief Draw a forecast weather icon.
 */
void drawWeatherIcon(Adafruit_GFX& gfx, int centerX, int topY, DayIcon icon);

/**
 * @brief Draw a smaller weather icon (~60% scale) for the today screen.
 */
void drawWeatherIconSmall(Adafruit_GFX& gfx, int centerX, int topY, DayIcon icon);

/**
 * @brief Compact wind label (auto-shrinks to fit in a forecast column).
 * @param text Example: "10 кмч"
 */
void drawWindLabel(Adafruit_GFX& gfx, int colLeft, int colRight, int y, const char* text);

/**
 * @brief Convert a UTF-8 string to a byte sequence supported by the current font.
 *
 * This project uses a font setup where Cyrillic characters are accessed via a
 * legacy single-byte mapping. This helper translates UTF-8 Cyrillic sequences
 * into that mapping.
 *
 * @param source UTF-8 input string.
 * @param out Output buffer for converted text.
 * @param outLen Size of output buffer.
 */
void utf8rus(const char* source, char* out, size_t outLen);

/** @brief Pre-convert Cyrillic weekday abbreviations (call once at startup). */
void initUtilLabels();

/**
 * @brief Format YYYY-MM-DD to a short numeric label (DD.MM) without heap allocations.
 * @param ymd Input expected: YYYY-MM-DD.
 * @param out Output buffer.
 */
void formatDateLabelDDMM(const char* ymd, char* out, size_t outLen);
