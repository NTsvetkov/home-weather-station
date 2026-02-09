#include "utils.h"

#include <Adafruit_ILI9341.h>
#include <math.h>

/**
 * @brief Calculate pixel width of text using classic Adafruit_GFX font.
 * @param text Text string.
 * @param size Text size multiplier.
 * @return Width in pixels.
 */
static inline uint16_t classicTextWidthPx(const char* text, uint8_t size) {
  if (!text) return 0;
  // Adafruit_GFX classic (built-in) font is 5x7 with 1px spacing => 6x8 cell.
  // With setTextSize(size): each cell scales by size.
  return (uint16_t)(strlen(text) * 6U * (uint16_t)size);
}

/** @brief Draw text centered horizontally at given X coordinate. */
void drawCenteredText(Adafruit_GFX& gfx, const char* text, int centerX, int y, uint8_t size, uint16_t color) {
  gfx.setTextSize(size);
  const uint16_t w = classicTextWidthPx(text, size);
  int drawX = centerX - ((int)w / 2);
  gfx.setTextColor(color);
  gfx.setCursor(drawX, y);
  gfx.print(text);
}

/** @brief Draw text right-aligned to given X coordinate. */
void drawRightAlignedText(Adafruit_GFX& gfx, const char* text, int rightX, int y, uint8_t size, uint16_t color) {
  gfx.setTextSize(size);
  const uint16_t w = classicTextWidthPx(text, size);
  int drawX = rightX - (int)w;
  gfx.setTextColor(color);
  gfx.setCursor(drawX, y);
  gfx.print(text);
}

/** @brief Draw a small triangle indicating trend direction. */
void drawTrendIndicator(Adafruit_GFX& gfx, int x, int y, int8_t trend) {
  // Simple, compact triangles: up / right / down.
  // This avoids clashing with the grid lines and keeps the meaning obvious.
  const int s = 12;
  if (trend > 0) {
    // Up
    gfx.fillTriangle(x, y + s, x + (s / 2), y, x + s, y + s, ILI9341_GREEN);
  } else if (trend < 0) {
    // Down
    gfx.fillTriangle(x, y, x + (s / 2), y + s, x + s, y, ILI9341_RED);
  } else {
    // Steady -> right
    gfx.fillTriangle(x, y, x + s, y + (s / 2), x, y + s, ILI9341_DARKGREY);
  }
}

/** @brief Draw a pressure bar with label and trend indicator. */
void drawPressureBar(Adafruit_GFX& gfx, int x, int y, float pressure, int8_t trend) {
  int barWidthMax = gfx.width() - (2 * x);
  if (barWidthMax < 50) barWidthMax = gfx.width() - 40;

  gfx.drawRect(x, y, barWidthMax, 20, ILI9341_WHITE);

  int barWidth = map((int)pressure, 970, 1050, 0, barWidthMax);
  barWidth = constrain(barWidth, 0, barWidthMax);
  gfx.fillRect(x, y, barWidth, 20, ILI9341_GREEN);

  // Format pressure as "1013.2 hPa" without heap allocations.
  const int p10  = (int)roundf(pressure * 10.0f);
  const int whole = p10 / 10;
  const int frac  = (p10 < 0) ? -(p10 % 10) : (p10 % 10);
  char pStr[24];
  snprintf(pStr, sizeof(pStr), "%d.%d hPa", whole, frac);
  gfx.setTextSize(3);
  const uint16_t w = classicTextWidthPx(pStr, 3);
  int textX = x + (barWidthMax / 2) - ((int)w / 2);
  gfx.setTextColor(ILI9341_WHITE);
  gfx.setCursor(textX, y + 26);
  gfx.print(pStr);

  // Pressure trend indicator (next to the text)
  drawTrendIndicator(gfx, textX + w + 8, y + 32, trend);
}

/** @brief Draw a weather icon (sun, cloud, rain, snow, storm) at given position. */
void drawWeatherIcon(Adafruit_GFX& gfx, int centerX, int topY, DayIcon icon) {
  // Slightly smaller icon block (more room for numbers and wind)
  int baseY = topY + 25;
  gfx.fillRect(centerX - 45, topY, 90, 60, ILI9341_BLACK);

  bool isThunder = (icon == ICON_STORM);
  bool isSnow    = (icon == ICON_SNOW);
  bool isRain    = (icon == ICON_RAIN || icon == ICON_HEAVY_RAIN || icon == ICON_STORM);
  bool isClear   = (icon == ICON_SUN);
  bool isPartly  = (icon == ICON_PARTLY);
  bool isCloudy  = (icon == ICON_CLOUDY || isRain || isSnow || isThunder || isPartly);

  // sun / partly cloudy sun
  if (isClear || isPartly) {
    // For "partly" icons move the sun a bit more to the left/up so it peeks out clearly.
    int sunX = isPartly ? (centerX - 20) : centerX;
    int sunY = isPartly ? (baseY - 9) : (baseY - 6);
    // Make the sun a bit larger for "partly" so it remains clearly visible.
    int sunR = isPartly ? 14 : 16;
    gfx.fillCircle(sunX, sunY, sunR, ILI9341_YELLOW);
    constexpr float RAY_STEP = PI / 4.0f;
    for (int i = 0; i < 8; i++) {
      float angle = i * RAY_STEP;
      int x1 = sunX + cos(angle) * (sunR + 5);
      int y1 = sunY + sin(angle) * (sunR + 6);
      int x2 = sunX + cos(angle) * (sunR + 11);
      int y2 = sunY + sin(angle) * (sunR + 11);
      // Thicker rays (draw twice with a 1px offset)
      gfx.drawLine(x1, y1, x2, y2, ILI9341_YELLOW);
      gfx.drawLine(x1 + 1, y1, x2 + 1, y2, ILI9341_YELLOW);
    }
    if (isClear) return;
    // if partly cloudy -> continue and draw cloud on top
  }

  // cloud base
  if (isCloudy || isRain || isSnow || isThunder) {
    // Slightly smaller clouds so the sun in "partly" icons remains visible.
    gfx.fillRoundRect(centerX - 31, baseY - 17, 62, 26, 8, ILI9341_LIGHTGREY);
    gfx.fillCircle(centerX - 15, baseY - 17, 11, ILI9341_LIGHTGREY);
    gfx.fillCircle(centerX + 10, baseY - 17, 13, ILI9341_LIGHTGREY);
  }

  if (isRain) {
    // Thicker rain streaks (3px) so they remain readable on the TFT.
    // Using fillRect also makes them look smoother than single-pixel lines.
    gfx.fillRect(centerX - 18, baseY + 4, 3, 18, ILI9341_CYAN);
    gfx.fillRect(centerX - 2, baseY + 6, 3, 18, ILI9341_CYAN);
    gfx.fillRect(centerX + 14, baseY + 4, 3, 18, ILI9341_CYAN);
  }
  if (icon == ICON_HEAVY_RAIN) {
    // Extra streaks for heavy rain
    gfx.fillRect(centerX - 34, baseY + 2, 3, 20, ILI9341_CYAN);
    gfx.fillRect(centerX + 31, baseY + 2, 3, 20, ILI9341_CYAN);
  }

  if (isSnow) {
    gfx.drawLine(centerX - 12, baseY + 6, centerX - 4, baseY + 14, ILI9341_WHITE);
    gfx.drawLine(centerX - 12, baseY + 14, centerX - 4, baseY + 6, ILI9341_WHITE);
    gfx.drawLine(centerX + 4, baseY + 6, centerX + 12, baseY + 14, ILI9341_WHITE);
    gfx.drawLine(centerX + 4, baseY + 14, centerX + 12, baseY + 6, ILI9341_WHITE);
  }

  if (isThunder) {
    gfx.drawLine(centerX - 6, baseY - 6, centerX + 2, baseY + 6, ILI9341_YELLOW);
    gfx.drawLine(centerX + 2, baseY + 6, centerX - 6, baseY + 6, ILI9341_YELLOW);
    gfx.drawLine(centerX - 6, baseY + 6, centerX + 6, baseY + 20, ILI9341_YELLOW);
  }
}

/** @brief Draw compact wind speed label (auto-shrinks to fit column). */
void drawWindLabel(Adafruit_GFX& gfx, int colLeft, int colRight, int y, const char* text) {
  const int colW = colRight - colLeft;
  // Keep it small so it never dominates the forecast column.
  uint8_t size = 2;

  // Fast auto-shrink for classic font: width is proportional to strlen(text).
  uint16_t w = classicTextWidthPx(text, size);
  while (size > 1 && w > (uint16_t)(colW - 6)) {
    size--;
    w = classicTextWidthPx(text, size);
  }
  int drawX = colRight - (int)w;
  gfx.setTextColor(ILI9341_WHITE);
  gfx.setCursor(drawX, y);
  gfx.print(text);
}

/**
 * @brief Convert UTF-8 Cyrillic to single-byte font encoding.
 * @param[in]  source UTF-8 input string.
 * @param[out] out    Output buffer for converted text.
 * @param[in]  outLen Size of output buffer.
 */
void utf8rus(const char* source, char* out, size_t outLen) {
  if (!source || !out || outLen == 0) return;

  size_t srcLen = strlen(source);
  size_t i      = 0;
  size_t outIdx = 0;

  while (i < srcLen && outIdx < outLen - 1) {
    unsigned char n = (unsigned char)source[i];
    i++;

    if (n >= 0xBF) {
      switch (n) {
        case 0xD0: {
          if (i >= srcLen) break;
          n = (unsigned char)source[i];
          i++;
          if (n == 0x81) {
            n = 0xA8;
            break;
          }
          if (n >= 0x90 && n <= 0xBF) n = (unsigned char)(n + 0x2F);
          break;
        }
        case 0xD1: {
          if (i >= srcLen) break;
          n = (unsigned char)source[i];
          i++;
          if (n == 0x91) {
            n = 0xB7;
            break;
          }
          if (n >= 0x80 && n <= 0x8F) n = (unsigned char)(n + 0x6F);
          break;
        }
      }
    }

    out[outIdx++] = (char)n;
  }

  out[outIdx] = '\0';
}

// Bulgarian weekday abbreviations (pre-converted in initUtilLabels()).
static char dowBuf[7][8];

/**
 * @brief Pre-convert Bulgarian weekday abbreviations via utf8rus().
 *
 * Must be called once at startup (from initDisplay()) before any
 * formatDateLabelDDMM() calls.
 */
void initUtilLabels() {
  utf8rus("Нд", dowBuf[0], sizeof(dowBuf[0]));  // 0 = Sunday
  utf8rus("Пн", dowBuf[1], sizeof(dowBuf[1]));  // 1 = Monday
  utf8rus("Вт", dowBuf[2], sizeof(dowBuf[2]));  // 2 = Tuesday
  utf8rus("Ср", dowBuf[3], sizeof(dowBuf[3]));  // 3 = Wednesday
  utf8rus("Чт", dowBuf[4], sizeof(dowBuf[4]));  // 4 = Thursday
  utf8rus("Пт", dowBuf[5], sizeof(dowBuf[5]));  // 5 = Friday
  utf8rus("Сб", dowBuf[6], sizeof(dowBuf[6]));  // 6 = Saturday
}

/**
 * @brief Format a YYYY-MM-DD date string to "Ден ДД" (e.g., "Пт 09").
 *
 * Calculates the day of week using Zeller's congruence and formats
 * the output with Bulgarian weekday abbreviation + day number.
 *
 * @param[in]  ymd    Input date string in ISO format (YYYY-MM-DD).
 * @param[out] out    Output buffer for formatted label.
 * @param[in]  outLen Size of output buffer (minimum 8 bytes).
 *
 * @note Weekday abbreviations: Нд, Пн, Вт, Ср, Чт, Пт, Сб
 */
void formatDateLabelDDMM(const char* ymd, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';
  if (!ymd) return;

  // Expected: YYYY-MM-DD (positions: 0-3 year, 5-6 month, 8-9 day)
  if (strlen(ymd) >= 10 && ymd[4] == '-' && ymd[7] == '-') {
    int year  = (ymd[0] - '0') * 1000 + (ymd[1] - '0') * 100 + (ymd[2] - '0') * 10 + (ymd[3] - '0');
    int month = (ymd[5] - '0') * 10 + (ymd[6] - '0');
    int day   = (ymd[8] - '0') * 10 + (ymd[9] - '0');

    // Zeller's congruence for day of week (inline for simplicity)
    int y = year, m = month;
    if (m < 3) { m += 12; y--; }
    int h = (day + (13 * (m + 1)) / 5 + (y % 100) + (y % 100) / 4 + (y / 100) / 4 - 2 * (y / 100)) % 7;
    int dow = ((h + 6) % 7);  // Convert to 0=Sunday..6=Saturday

    if (outLen >= 8) {
      snprintf(out, outLen, "%s %02d", dowBuf[dow], day);
      return;
    }
  }

  // Fallback: best-effort copy
  strncpy(out, ymd, outLen);
  out[outLen - 1] = '\0';
}
