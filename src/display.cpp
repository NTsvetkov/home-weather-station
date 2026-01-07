#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include "display.h"
#include "data.h"

/**
 * @file display.cpp
 * @brief TFT UI rendering (main screen + forecast screen).
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

/**
 * @brief Choose a daily icon based on precipitation / temperature / cloud cover.
 *
 * Wind is handled separately as a label in the UI.
 */
static DayIcon pickDayIcon(float tMax, float tMin, float precipSum, float cloudMean, int wmoCode) {
  // Thunderstorms: allow WMO to override
  if (wmoCode == 95 || wmoCode == 96 || wmoCode == 99) return ICON_STORM;

  // Precipitation first
  if (precipSum >= 10.0f) {
    if (tMax <= 2.0f) return ICON_SNOW;

    return ICON_HEAVY_RAIN;
  }
  if (precipSum >= 0.2f) {
    if (tMax <= 2.0f) return ICON_SNOW;

    return ICON_RAIN;
  }

  // Sky by cloud cover
  if (cloudMean < 25.0f) return ICON_SUN;
  if (cloudMean < 70.0f) return ICON_PARTLY;

  return ICON_CLOUDY;
}

#define TFT_CS    D8
#define TFT_RST   D3
#define TFT_DC    D4

static Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

/**
 * @brief Convert UTF-8 string to a byte sequence supported by the current font.
 *
 * Historical helper kept for Cyrillic text rendering.
 */
String utf8rus(String source) {
  int i, k;
  String target;
  target.reserve(source.length()); // avoid repeated allocations on small heap
  unsigned char n;
  char m[2] = { '0', '\0' };

  k = source.length();
  i = 0;

  while (i < k) {
    n = source[i]; i++;

    if (n >= 0xBF) {
      switch (n) {
        case 0xD0: {
          n = source[i]; i++;
          if (n == 0x81) { n = 0xA8; break; }
          if (n >= 0x90 && n <= 0xBF) n = n + 0x2F;
          break;
        }
        case 0xD1: {
          n = source[i]; i++;
          if (n == 0x91) { n = 0xB7; break; }
          if (n >= 0x80 && n <= 0x8F) n = n + 0x6F;
          break;
        }
      }
    }
    m[0] = n; target = target + String(m);
  }

  return target;
}

/** @brief Pick a UI color for a temperature value (C). */
uint16_t colorForTemperature(float tempC) {
  if (tempC < 10) return ILI9341_CYAN;
  if (tempC < 18) return ILI9341_BLUE;
  if (tempC <= 25) return ILI9341_GREEN;
  if (tempC <= 30) return ILI9341_ORANGE;

  return ILI9341_RED;
}

/** @brief Pick a UI color for a humidity value (%). */
uint16_t colorForHumidity(float humidity) {
  if (humidity < 30) return ILI9341_CYAN;
  if (humidity < 40) return ILI9341_BLUE;
  if (humidity <= 60) return ILI9341_GREEN;
  if (humidity <= 70) return ILI9341_ORANGE;

  return ILI9341_RED;
}

static int dayOfWeek(int y, int m, int d) {
  // Tomohiko Sakamoto algorithm: 0=Sunday..6=Saturday
  static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;

  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

String labelForDate(const String& dateStr) {
  // Input expected: YYYY-MM-DD
  if (dateStr.length() != 10) return dateStr;

  int y = dateStr.substring(0, 4).toInt();
  int m = dateStr.substring(5, 7).toInt();
  int d = dateStr.substring(8, 10).toInt();

  static const char* dowBg[] = {"Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
  int dow                    = dayOfWeek(y, m, d);
  if (dow < 0 || dow > 6) dow = 0;

  // e.g. "Чт 01"
  String dd = dateStr.substring(8);

  return String(dowBg[dow]) + " " + dd;
}

/** @brief Draw text centered around centerX. */
void drawCenteredText(const String& text, int centerX, int y, uint8_t size, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(size);
  tft.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
  int drawX = centerX - (w / 2);
  tft.setTextColor(color);
  tft.setCursor(drawX, y);
  tft.print(text);
}

/** @brief Draw text right-aligned to rightX. */
void drawRightAlignedText(const String& text, int rightX, int y, uint8_t size, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(size);
  tft.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
  int drawX = rightX - w;
  tft.setTextColor(color);
  tft.setCursor(drawX, y);
  tft.print(text);
}

/**
 * @brief Compact wind label (auto-shrinks to fit in the forecast column).
 * @param text Example: "10 km"
 */
static void drawWindLabel(int colLeft, int colRight, int y, const String& text) {
  const int colW = colRight - colLeft;
  // Keep it small so it never dominates the forecast column.
  uint8_t size = 2;

  int16_t x1, y1;
  uint16_t w, h;
  for (;;) {
    tft.setTextSize(size);
    tft.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
    if (w <= (uint16_t)(colW - 6) || size <= 1) break;
    size--;
  }

  // Draw as plain text (no big badge box) to guarantee it fits.
  int drawX = colRight - w;
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(drawX, y);
  tft.print(text);
}

/**
 * @brief Format large temperature values so they always fit in the main screen cells.
 */
static String formatBigTemp(float t) {
  if (t < -10.0f) {
    // Very cold: show as integer to save space
    return String((int)roundf(t));
  }

  // All other values: show one decimal
  return String(t, 1);
}

// Forward declaration (used before the definition further below)
static void drawTrendIndicator(int x, int y, int8_t trend);

void drawPressureBar(int x, int y, float pressure) {
  int barWidthMax = tft.width() - (2 * x);
  if (barWidthMax < 50) barWidthMax = tft.width() - 40;

  tft.drawRect(x, y, barWidthMax, 20, ILI9341_WHITE);

  int barWidth = map(pressure, 970, 1050, 0, barWidthMax);
  barWidth     = constrain(barWidth, 0, barWidthMax);
  tft.fillRect(x, y, barWidth, 20, ILI9341_GREEN);

  String pStr = String(pressure, 1) + " hPa";
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(3);
  tft.getTextBounds(pStr.c_str(), 0, 0, &x1, &y1, &w, &h);
  int textX = x + (barWidthMax / 2) - (w / 2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(textX, y + 26);
  tft.print(pStr);

  // Pressure trend indicator (next to the text)
  drawTrendIndicator(textX + w + 8, y + 32, extPressTrend);
}

void drawWeatherIcon(int centerX, int topY, DayIcon icon) {
  // Slightly smaller icon block (more room for numbers and wind)
  int baseY = topY + 25;
  tft.fillRect(centerX - 45, topY, 90, 60, ILI9341_BLACK);

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
    tft.fillCircle(sunX, sunY, sunR, ILI9341_YELLOW);
    for (int i = 0; i < 8; i++) {
      float angle = i * PI / 4;
      int x1 = sunX + cos(angle) * (sunR + 5);
      int y1 = sunY + sin(angle) * (sunR + 6);
      int x2 = sunX + cos(angle) * (sunR + 11);
      int y2 = sunY + sin(angle) * (sunR + 11);
      // Thicker rays (draw twice with a 1px offset)
      tft.drawLine(x1, y1, x2, y2, ILI9341_YELLOW);
      tft.drawLine(x1 + 1, y1, x2 + 1, y2, ILI9341_YELLOW);
    }
    if (isClear) return;
    // if partly cloudy -> continue and draw cloud on top
  }

  // cloud base
  if (isCloudy || isRain || isSnow || isThunder) {
    // Slightly smaller clouds so the sun in "partly" icons remains visible.
    tft.fillRoundRect(centerX - 31, baseY - 17, 62, 26, 8, ILI9341_LIGHTGREY);
    tft.fillCircle(centerX - 15, baseY - 17, 11, ILI9341_LIGHTGREY);
    tft.fillCircle(centerX + 10, baseY - 17, 13, ILI9341_LIGHTGREY);
  }

  if (isRain) {
    // Thicker rain streaks (3px) so they remain readable on the TFT.
    // Using fillRect also makes them look smoother than single-pixel lines.
    tft.fillRect(centerX - 18, baseY + 4, 3, 18, ILI9341_CYAN);
    tft.fillRect(centerX - 2,  baseY + 6, 3, 18, ILI9341_CYAN);
    tft.fillRect(centerX + 14, baseY + 4, 3, 18, ILI9341_CYAN);
  }
  if (icon == ICON_HEAVY_RAIN) {
    // Extra streaks for heavy rain
    tft.fillRect(centerX - 34, baseY + 2, 3, 20, ILI9341_CYAN);
    tft.fillRect(centerX + 31, baseY + 2, 3, 20, ILI9341_CYAN);
  }


  if (isSnow) {
    tft.drawLine(centerX - 12, baseY + 6, centerX - 4, baseY + 14, ILI9341_WHITE);
    tft.drawLine(centerX - 12, baseY + 14, centerX - 4, baseY + 6, ILI9341_WHITE);
    tft.drawLine(centerX + 4, baseY + 6, centerX + 12, baseY + 14, ILI9341_WHITE);
    tft.drawLine(centerX + 4, baseY + 14, centerX + 12, baseY + 6, ILI9341_WHITE);
  }

  if (isThunder) {
    tft.drawLine(centerX - 6, baseY - 6, centerX + 2, baseY + 6, ILI9341_YELLOW);
    tft.drawLine(centerX + 2, baseY + 6, centerX - 6, baseY + 6, ILI9341_YELLOW);
    tft.drawLine(centerX - 6, baseY + 6, centerX + 6, baseY + 20, ILI9341_YELLOW);
  }
}

/** @brief Small triangle indicator for trends (-1/0/+1). */
static void drawTrendIndicator(int x, int y, int8_t trend) {
  // Simple, compact triangles: up / right / down.
  // This avoids clashing with the grid lines and keeps the meaning obvious.
  const int s = 12;
  if (trend > 0) {
    // Up
    tft.fillTriangle(x, y + s, x + (s / 2), y, x + s, y + s, ILI9341_GREEN);
  } else if (trend < 0) {
    // Down
    tft.fillTriangle(x, y, x + (s / 2), y + s, x + s, y, ILI9341_RED);
  } else {
    // Steady -> right
    tft.fillTriangle(x, y, x + s, y + (s / 2), x, y + s, ILI9341_DARKGREY);
  }
}

/** @brief Render the main readings screen. */
void drawMainScreen() {
  tft.fillScreen(ILI9341_BLACK);

  const int leftCenterX  = tft.width() / 4;
  const int rightCenterX = (tft.width() * 3) / 4;

  const int labelY    = 8;
  const int tempY     = 48;
  const int humidityY = 130;

  int midX           = tft.width() / 2;
  int verticalHeight = 175;
  tft.drawFastVLine(midX, 0, verticalHeight, ILI9341_DARKGREY);
  tft.drawFastVLine(midX + 1, 0, verticalHeight, ILI9341_DARKGREY);
  tft.drawFastHLine(0, 35, tft.width(), ILI9341_DARKGREY);
  tft.drawFastHLine(0, 36, tft.width(), ILI9341_DARKGREY);
  tft.drawFastHLine(0, 115, tft.width(), ILI9341_DARKGREY);
  tft.drawFastHLine(0, 116, tft.width(), ILI9341_DARKGREY);
  tft.drawFastHLine(0, 175, tft.width(), ILI9341_DARKGREY);
  tft.drawFastHLine(0, 176, tft.width(), ILI9341_DARKGREY);

  drawCenteredText(utf8rus("навън"), leftCenterX, labelY, 3, ILI9341_WHITE);
  if (haveExtData) {
    // Format so values like -10.0, 12.3 always fit in the cell
    drawCenteredText(formatBigTemp(extTemperature), leftCenterX + 5, tempY, 6, colorForTemperature(extTemperature));

    // Trend indicator: keep it near the left edge so it doesn't clash with the right-aligned numbers.
    const int triX = 6;
    drawTrendIndicator(triX, tempY + 48, extTempTrend);

    // External humidity: right aligned, with the trend triangle under it.
    const int extHumY = humidityY - 4;
    drawRightAlignedText(String(extHumidity, 0) + " %", midX - 10, humidityY, 4, colorForHumidity(extHumidity));
    drawTrendIndicator(triX, extHumY + 30, extHumTrend);
  } else {
    drawCenteredText(utf8rus("няма данни"), leftCenterX, tempY, 3, ILI9341_YELLOW);
  }

  drawCenteredText(utf8rus("вътре"), rightCenterX, labelY, 3, ILI9341_WHITE);
  if (haveIntData) {
    // Keep indoor temperature formatting as before (no need for negative-fit logic here)
    drawCenteredText(String(intTemperature, 1), rightCenterX + 5, tempY, 6, colorForTemperature(intTemperature));
    drawCenteredText(String(intHumidity, 0) + " %", rightCenterX, humidityY, 4, colorForHumidity(intHumidity));
  } else {
    drawCenteredText(utf8rus("няма данни"), rightCenterX, tempY, 3, ILI9341_YELLOW);
  }

  if (haveExtData) drawPressureBar(20, 190, extPressure);
}

/** @brief Render the forecast screen. */
void drawForecastScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawCenteredText(utf8rus("прогноза"), tft.width() / 2, 4, 3, ILI9341_WHITE);
  tft.drawFastHLine(0, 35, tft.width(), ILI9341_DARKGREY); // line under title

  if (forecastCount == 0) {
    drawCenteredText(utf8rus("няма прогноза"), tft.width() / 2, 120, 3, ILI9341_YELLOW);

    return;
  }

  int colW = tft.width() / 3;
  for (int i = 0; i < forecastCount && i < 3; i++) {
    int colStart  = i * colW;
    int colCenter = colStart + (colW / 2);
    int colRight  = colStart + colW - 6; // small padding from right edge

    drawCenteredText(utf8rus(labelForDate(forecast[i].label)), colCenter, 45, 2, ILI9341_CYAN);
    DayIcon icon = pickDayIcon(forecast[i].tMax, forecast[i].tMin, forecast[i].precip, forecast[i].cloudMean, forecast[i].wmoCode);
    drawWeatherIcon(colCenter, 72, icon);

    String maxStr  = String(forecast[i].tMax, 1) + " C";
    String minStr  = String(forecast[i].tMin, 1) + " C";
    int rainLiters = (int)(forecast[i].precip + 0.5f);
    String rainStr = String(rainLiters) + utf8rus(" Л");

    // Numbers a bit higher to free space for a readable wind badge
    drawRightAlignedText(maxStr, colRight, 146, 2, ILI9341_WHITE);
    drawRightAlignedText(minStr, colRight, 168, 2, ILI9341_WHITE);

    int rainY = 196;
    int dropX = colRight - 90; // move left to avoid overlapping text
    tft.fillTriangle(dropX, rainY - 12, dropX - 6, rainY - 2, dropX + 6, rainY - 2, ILI9341_BLUE);
    tft.fillCircle(dropX, rainY - 1, 4, ILI9341_BLUE);
    drawRightAlignedText(rainStr, colRight, rainY - 6, 2, ILI9341_BLUE);

    // Wind (compact): e.g. "10 km". Auto-shrinks if needed.
    String windStr = String((int)(forecast[i].windMax + 0.5f)) + utf8rus(" кмч");
    drawWindLabel(colStart + 6, colRight, 212, windStr);

  }

  // vertical separators between days
  int sepYTop    = 22;
  int sepYBottom = 240;
  for (int i = 1; i < 3; i++) {
    int x = i * colW;
    tft.drawFastVLine(x, sepYTop, sepYBottom - sepYTop, ILI9341_DARKGREY);
  }

  // horizontal line under icons
  tft.drawFastHLine(0, 135, tft.width(), ILI9341_DARKGREY);
}

void initDisplay() {
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
}
