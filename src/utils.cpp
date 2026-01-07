#include "utils.h"

#include <Adafruit_ILI9341.h>
#include <math.h>

static inline uint16_t classicTextWidthPx(const char* text, uint8_t size) {
  if (!text) return 0;
  // Adafruit_GFX classic (built-in) font is 5x7 with 1px spacing => 6x8 cell.
  // With setTextSize(size): each cell scales by size.
  return (uint16_t)(strlen(text) * 6U * (uint16_t)size);
}

void drawCenteredText(Adafruit_GFX& gfx, const char* text, int centerX, int y, uint8_t size, uint16_t color) {
  gfx.setTextSize(size);
  const uint16_t w = classicTextWidthPx(text, size);
  int drawX = centerX - ((int)w / 2);
  gfx.setTextColor(color);
  gfx.setCursor(drawX, y);
  gfx.print(text);
}

void drawRightAlignedText(Adafruit_GFX& gfx, const char* text, int rightX, int y, uint8_t size, uint16_t color) {
  gfx.setTextSize(size);
  const uint16_t w = classicTextWidthPx(text, size);
  int drawX = rightX - (int)w;
  gfx.setTextColor(color);
  gfx.setCursor(drawX, y);
  gfx.print(text);
}

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
    for (int i = 0; i < 8; i++) {
      float angle = i * PI / 4;
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

void drawWindLabel(Adafruit_GFX& gfx, int colLeft, int colRight, int y, const char* text) {
  const int colW = colRight - colLeft;
  // Keep it small so it never dominates the forecast column.
  uint8_t size = 2;

  // Fast auto-shrink for classic font: width is proportional to strlen(text).
  while (size > 1) {
    const uint16_t w = classicTextWidthPx(text, size);
    if (w <= (uint16_t)(colW - 6)) break;
    size--;
  }

  // Draw as plain text (no big badge box) to guarantee it fits.
  const uint16_t w = classicTextWidthPx(text, size);
  int drawX = colRight - (int)w;
  gfx.setTextColor(ILI9341_WHITE);
  gfx.setCursor(drawX, y);
  gfx.print(text);
}

void utf8rus(const char* source, char* out, size_t outLen) {
  if (!source || !out || outLen == 0) return;

  size_t srcLen = strlen(source);
  size_t i = 0;
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

void formatDateLabelDDMM(const char* ymd, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';

  if (!ymd) return;

  // Expected: YYYY-MM-DD
  // Positions: 0..3 year, 4 '-', 5..6 month, 7 '-', 8..9 day
  if (strlen(ymd) >= 10 && ymd[4] == '-' && ymd[7] == '-') {
    if (outLen < 6) return; // "DD.MM" + '\0'
    out[0] = ymd[8];
    out[1] = ymd[9];
    out[2] = '.';
    out[3] = ymd[5];
    out[4] = ymd[6];
    out[5] = '\0';
    return;
  }

  // Fallback: best-effort copy.
  strncpy(out, ymd, outLen);
  out[outLen - 1] = '\0';
}
