#include <Arduino.h>

#include "display_config.h"
#include "display.h"
#include "data.h"
#include "utils.h"

/**
 * @file display.cpp
 * @brief TFT UI rendering (main screen + forecast screen).
 */

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

static TftDriver tft(TFT_CS, TFT_DC, TFT_RST);

// Pre-converted Cyrillic labels (initialized once in initDisplay()).
static char labelOutside[16];
static char labelInside[16];
static char labelNoData1[16];
static char labelNoData2[16];
static char labelForecast[24];
static char labelNoForecast[32];
static char labelToday[16];
static char labelNoHourly[32];
static char unitLiters[8];
static char unitKmh[12];

/**
 * @brief Format temperature with unit as "-12.3 C".
 * @param[out] out    Output buffer.
 * @param[in]  outLen Buffer size.
 * @param[in]  tempC  Temperature in Celsius.
 */
static void formatTempC1(char* out, size_t outLen, float tempC) {
  // Format as "-12.3 C" without using String(float, 1) to avoid heap churn.
  const int t10   = (int)roundf(tempC * 10.0f);
  const int abs10 = (t10 < 0) ? -t10 : t10;
  const int whole = abs10 / 10;
  const int frac  = abs10 % 10;

  if (t10 < 0) {
    snprintf(out, outLen, "-%d.%d C", whole, frac);
  } else {
    snprintf(out, outLen, "%d.%d C", whole, frac);
  }
}

/**
 * @brief Format temperature without unit as "-12.3".
 * @param[out] out    Output buffer.
 * @param[in]  outLen Buffer size.
 * @param[in]  tempC  Temperature in Celsius.
 */
static void formatTemp1NoUnit(char* out, size_t outLen, float tempC) {
  // Format as "-12.3" (1 decimal) without heap allocations.
  const int t10   = (int)roundf(tempC * 10.0f);
  const int abs10 = (t10 < 0) ? -t10 : t10;
  const int whole = abs10 / 10;
  const int frac  = abs10 % 10;

  if (t10 < 0) {
    snprintf(out, outLen, "-%d.%d", whole, frac);
  } else {
    snprintf(out, outLen, "%d.%d", whole, frac);
  }
}

/**
 * @brief Format temperature for large display (integer if < -10C, else 1 decimal).
 * @param[out] out    Output buffer.
 * @param[in]  outLen Buffer size.
 * @param[in]  tempC  Temperature in Celsius.
 */
static void formatBigTempNoUnit(char* out, size_t outLen, float tempC) {
  // Match utils::formatBigTemp(): < -10C -> integer, otherwise 1 decimal.
  if (tempC < -10.0f) {
    snprintf(out, outLen, "%d", (int)roundf(tempC));
    return;
  }

  formatTemp1NoUnit(out, outLen, tempC);
}

/**
 * @brief Format value as integer percentage "55 %".
 * @param[out] out    Output buffer.
 * @param[in]  outLen Buffer size.
 * @param[in]  value  Value to format.
 */
static void formatPercent0(char* out, size_t outLen, float value) {
  // Format as "55 %" without String(value, 0) + " %".
  const int v = (int)roundf(value);
  snprintf(out, outLen, "%d %%", v);
}

/** @brief Pick a UI color for a temperature value (C). */
uint16_t colorForTemperature(float tempC) {
  if (tempC < 10) return CLR_CYAN;
  if (tempC < 18) return CLR_BLUE;
  if (tempC <= 25) return CLR_GREEN;
  if (tempC <= 30) return CLR_ORANGE;

  return CLR_RED;
}

/** @brief Pick a UI color for a humidity value (%). */
uint16_t colorForHumidity(float humidity) {
  if (humidity < 30) return CLR_CYAN;
  if (humidity < 40) return CLR_BLUE;
  if (humidity <= 60) return CLR_GREEN;
  if (humidity <= 70) return CLR_ORANGE;

  return CLR_RED;
}
/** @brief Render the main readings screen. */
void drawMainScreen() {
  tft.fillScreen(CLR_BLACK);

  const int leftCenterX  = tft.width() / 4;
  const int rightCenterX = (tft.width() * 3) / 4;

  const int labelY    = 8;
  const int tempY     = 48;
  const int humidityY = 130;

  int midX           = tft.width() / 2;
  int verticalHeight = 175;
  tft.drawFastVLine(midX, 0, verticalHeight, CLR_DARKGREY);
  tft.drawFastVLine(midX + 1, 0, verticalHeight, CLR_DARKGREY);
  tft.drawFastHLine(0, 35, tft.width(), CLR_DARKGREY);
  tft.drawFastHLine(0, 36, tft.width(), CLR_DARKGREY);
  tft.drawFastHLine(0, 115, tft.width(), CLR_DARKGREY);
  tft.drawFastHLine(0, 116, tft.width(), CLR_DARKGREY);
  tft.drawFastHLine(0, 175, tft.width(), CLR_DARKGREY);
  tft.drawFastHLine(0, 176, tft.width(), CLR_DARKGREY);

  drawCenteredText(tft, labelOutside, leftCenterX, labelY, 3, CLR_WHITE);
  if (haveExtData) {
    // Format so values like -10.0, 12.3 always fit in the cell
    char extTempStr[16];
    formatBigTempNoUnit(extTempStr, sizeof(extTempStr), extTemperature);
    drawCenteredText(tft, extTempStr, leftCenterX + 5, tempY, 6, colorForTemperature(extTemperature));

    // Trend indicator: keep it near the left edge so it doesn't clash with the right-aligned numbers.
    const int triX = 6;
    drawTrendIndicator(tft, triX, tempY + 48, extTempTrend);

    // External humidity: right aligned, with the trend triangle under it.
    const int extHumY = humidityY - 4;
    char extHumStr[10];
    formatPercent0(extHumStr, sizeof(extHumStr), extHumidity);
    drawRightAlignedText(tft, extHumStr, midX - 10, humidityY, 4, colorForHumidity(extHumidity));
    drawTrendIndicator(tft, triX, extHumY + 30, extHumTrend);
  } else {
    drawCenteredText(tft, labelNoData1, leftCenterX, tempY, 3, CLR_YELLOW);
    drawCenteredText(tft, labelNoData2, leftCenterX, tempY + 26, 3, CLR_YELLOW);
  }

  drawCenteredText(tft, labelInside, rightCenterX, labelY, 3, CLR_WHITE);
  if (haveIntData) {
    // Keep indoor temperature formatting as before (no need for negative-fit logic here)
    char intTempStr[16];
    formatTemp1NoUnit(intTempStr, sizeof(intTempStr), intTemperature);
    drawCenteredText(tft, intTempStr, rightCenterX + 5, tempY, 6, colorForTemperature(intTemperature));

    char intHumStr[10];
    formatPercent0(intHumStr, sizeof(intHumStr), intHumidity);
    drawCenteredText(tft, intHumStr, rightCenterX, humidityY, 4, colorForHumidity(intHumidity));
  } else {
    drawCenteredText(tft, labelNoData1, rightCenterX, tempY, 3, CLR_YELLOW);
    drawCenteredText(tft, labelNoData2, rightCenterX, tempY + 26, 3, CLR_YELLOW);
  }

  if (haveExtData) drawPressureBar(tft, 20, 190, extPressure, extPressTrend);
}

/** @brief Render the forecast screen. */
void drawForecastScreen() {
  tft.fillScreen(CLR_BLACK);

  drawCenteredText(tft, labelForecast, tft.width() / 2, 4, 3, CLR_WHITE);
  tft.drawFastHLine(0, 35, tft.width(), CLR_DARKGREY); // line under title

  if (forecastCount == 0) {
    drawCenteredText(tft, labelNoForecast, tft.width() / 2, 120, 3, CLR_YELLOW);

    return;
  }

  int colW = tft.width() / 3;

  for (int i = 0; i < forecastCount && i < 3; i++) {
    int colStart  = i * colW;
    int colCenter = colStart + (colW / 2);
    int colRight  = colStart + colW - 6; // small padding from right edge

    char dayLabel[8];
    formatDateLabelDDMM(forecast[i].label, dayLabel, sizeof(dayLabel));
    drawCenteredText(tft, dayLabel, colCenter, 45, 2, CLR_CYAN);
    DayIcon icon = pickDayIcon(forecast[i].tMax, forecast[i].tMin, forecast[i].precip, forecast[i].cloudMean, forecast[i].wmoCode);
    drawWeatherIcon(tft, colCenter, 72, icon);

    char maxStr[16];
    char minStr[16];
    formatTempC1(maxStr, sizeof(maxStr), forecast[i].tMax);
    formatTempC1(minStr, sizeof(minStr), forecast[i].tMin);
    int rainLiters = (int)(forecast[i].precip + 0.5f);
    char rainStr[24];
    snprintf(rainStr, sizeof(rainStr), "%d%s", rainLiters, unitLiters);

    // Numbers a bit higher to free space for a readable wind badge
    drawRightAlignedText(tft, maxStr, colRight, 146, 2, CLR_WHITE);
    drawRightAlignedText(tft, minStr, colRight, 168, 2, CLR_WHITE);

    int rainY = 196;
    int dropX = colRight - 90; // move left to avoid overlapping text
    tft.fillTriangle(dropX, rainY - 12, dropX - 6, rainY - 2, dropX + 6, rainY - 2, CLR_BLUE);
    tft.fillCircle(dropX, rainY - 1, 4, CLR_BLUE);
    drawRightAlignedText(tft, rainStr, colRight, rainY - 6, 2, CLR_BLUE);

    // Wind (compact): e.g. "10 km". Auto-shrinks if needed.
    char windStr[24];
    snprintf(windStr, sizeof(windStr), "%d%s", (int)(forecast[i].windMax + 0.5f), unitKmh);
    drawWindLabel(tft, colStart + 6, colRight, 212, windStr);
  }

  // vertical separators between days
  int sepYTop    = 22;
  int sepYBottom = 240;
  for (int i = 1; i < 3; i++) {
    int x = i * colW;
    tft.drawFastVLine(x, sepYTop, sepYBottom - sepYTop, CLR_DARKGREY);
  }

  // horizontal line under icons
  tft.drawFastHLine(0, 135, tft.width(), CLR_DARKGREY);
}

/** @brief Time range labels for the 4 blocks. */
static const char* const blockLabels[4] = {"00-06", "06-12", "12-18", "18-24"};

/** @brief Render the today 6-hour forecast screen (2x2 grid). */
void drawTodayScreen() {
  tft.fillScreen(CLR_BLACK);

  if (!haveTodayForecast) {
    drawCenteredText(tft, labelToday, tft.width() / 2, 80, 3, CLR_WHITE);
    drawCenteredText(tft, labelNoHourly, tft.width() / 2, 120, 3, CLR_YELLOW);
    return;
  }

  const int cellW = tft.width() / 2;   // 160
  const int cellH = tft.height() / 2;  // 120

  // Grid separator lines
  tft.drawFastVLine(cellW, 0, tft.height(), CLR_DARKGREY);
  tft.drawFastVLine(cellW + 1, 0, tft.height(), CLR_DARKGREY);
  tft.drawFastHLine(0, cellH, tft.width(), CLR_DARKGREY);
  tft.drawFastHLine(0, cellH + 1, tft.width(), CLR_DARKGREY);

  for (int b = 0; b < 4; b++) {
    int col = b % 2;
    int row = b / 2;
    int x0  = col * cellW;
    int y0  = row * cellH;
    int cx  = x0 + cellW / 2;

    // Time range label (+3px down)
    drawCenteredText(tft, blockLabels[b], cx, y0 + 7, 2, CLR_CYAN);

    if (!todayBlocks[b].valid) {
      drawCenteredText(tft, "--", cx, y0 + 53, 2, CLR_DARKGREY);
      continue;
    }

    // Weather icon (small)
    DayIcon icon = pickDayIcon(
      todayBlocks[b].tMax, todayBlocks[b].tMin,
      todayBlocks[b].precip, todayBlocks[b].cloudMean,
      todayBlocks[b].wmoCode
    );
    drawWeatherIconSmall(tft, cx, y0 + 29, icon);

    // Temperature: "max / min" with drawn degree circles
    char maxStr[8];
    char minStr[8];
    int tMaxI = (int)roundf(todayBlocks[b].tMax);
    int tMinI = (int)roundf(todayBlocks[b].tMin);
    snprintf(maxStr, sizeof(maxStr), "%d", tMaxI);
    snprintf(minStr, sizeof(minStr), "%d", tMinI);

    // Build "max / min" for centering calculation
    char tempStr[20];
    snprintf(tempStr, sizeof(tempStr), "%s  / %s ", maxStr, minStr);
    float avgTemp = (todayBlocks[b].tMax + todayBlocks[b].tMin) / 2.0f;
    int tempY = y0 + 70;
    drawCenteredText(tft, tempStr, cx, tempY, 2, colorForTemperature(avgTemp));

    // Draw degree circles after each number
    // At size 2, each char is 12px wide, 16px tall
    int tempTotalW = (int)strlen(tempStr) * 12;
    int tempStartX = cx - tempTotalW / 2;
    int maxEndX = tempStartX + (int)strlen(maxStr) * 12;
    tft.drawCircle(maxEndX + 2, tempY + 1, 2, colorForTemperature(avgTemp));
    int minEndX = tempStartX + ((int)strlen(maxStr) + 4 + (int)strlen(minStr)) * 12;
    tft.drawCircle(minEndX + 2, tempY + 1, 2, colorForTemperature(avgTemp));

    // Precipitation + Wind on bottom line
    int rainI = (int)(todayBlocks[b].precip + 0.5f);
    int windI = (int)(todayBlocks[b].windMax + 0.5f);

    // Rain drop icon + value on left side of cell
    int infoY = y0 + 99;
    int dropX = x0 + 18;
    tft.fillTriangle(dropX, infoY - 8, dropX - 4, infoY, dropX + 4, infoY, CLR_BLUE);
    tft.fillCircle(dropX, infoY + 1, 3, CLR_BLUE);

    char rainStr[8];
    snprintf(rainStr, sizeof(rainStr), "%d", rainI);
    tft.setTextSize(2);
    tft.setTextColor(CLR_BLUE);
    tft.setCursor(dropX + 8, infoY - 6);
    tft.print(rainStr);

    // Wind flag icon + value on right side of cell
    int windRightX = x0 + cellW - 8;
    // Small wind flag: a pole with a pennant
    int flagX = x0 + cellW / 2 + 14;
    int flagTopY = infoY - 8;
    tft.drawFastVLine(flagX, flagTopY, 14, CLR_WHITE);           // pole
    tft.fillTriangle(flagX + 1, flagTopY, flagX + 10, flagTopY + 3,
                     flagX + 1, flagTopY + 6, CLR_WHITE);        // pennant

    char windStr[8];
    snprintf(windStr, sizeof(windStr), "%d", windI);
    drawRightAlignedText(tft, windStr, windRightX, infoY - 6, 2, CLR_WHITE);
  }
}

/**
 * @brief Initialize TFT display (rotation, text settings, clear screen).
 */
void initDisplay() {
#if defined(DISPLAY_ST7789)
  tft.init(240, 320);           // ST7789: explicit panel resolution
  tft.invertDisplay(false);     // ST7789 defaults to inverted; undo for this panel
  tft.setRotation(3);           // Landscape 320x240
#elif defined(DISPLAY_ILI9341)
  tft.begin();
  tft.setRotation(1);           // Landscape 320x240
#endif
  // cp437(false) = classic mode: chars >= 176 get c++ in drawChar().
  // This matches the utf8rus() mapping where А = 0xBF -> font pos 0xC0.
  tft.cp437(false);
  tft.setTextWrap(false);
  tft.fillScreen(CLR_BLACK);

  // Pre-convert all Cyrillic labels once at startup.
  utf8rus("навън", labelOutside, sizeof(labelOutside));
  utf8rus("вътре", labelInside, sizeof(labelInside));
  utf8rus("няма", labelNoData1, sizeof(labelNoData1));
  utf8rus("данни", labelNoData2, sizeof(labelNoData2));
  utf8rus("прогноза", labelForecast, sizeof(labelForecast));
  utf8rus("няма прогноза", labelNoForecast, sizeof(labelNoForecast));
  utf8rus("днес", labelToday, sizeof(labelToday));
  utf8rus("няма данни", labelNoHourly, sizeof(labelNoHourly));
  utf8rus(" Л", unitLiters, sizeof(unitLiters));
  utf8rus(" кмч", unitKmh, sizeof(unitKmh));

  initUtilLabels();
}
