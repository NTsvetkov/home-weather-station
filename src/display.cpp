#include <Arduino.h>

#include "display_config.h"
#include "display.h"
#include "data.h"
#include "utils.h"

/**
 * @file display.cpp
 * @brief TFT UI rendering (status bar + main / 3-day / today screens).
 *
 * Layout (320x240, classic 5x7 font, cell = 6*size x 8*size px):
 *   y 0..21   status bar: WiFi bars | screen name | last update time
 *   y 22      divider
 *   main:     big temps (size 6) at y=34, pictogram + humidity (size 4) at y=90,
 *             divider y=130, pressure y=142, scale y=193, scale labels y=206
 *   forecast: header y=32, icon 56.., max y=118 (size 3), min y=146,
 *             divider y=170, rain y=184, wind y=210
 */

// ---- Freshness thresholds for the "last update" time (seconds) ----
#ifndef CFG_GAUGE_STALE_WARN_S
#define CFG_GAUGE_STALE_WARN_S      600UL    // > 10 min: amber
#endif
#ifndef CFG_GAUGE_STALE_OLD_S
#define CFG_GAUGE_STALE_OLD_S       1800UL   // > 30 min: red + outdoor values greyed out
#endif
#ifndef CFG_FORECAST_STALE_WARN_S
#define CFG_FORECAST_STALE_WARN_S   5400UL   // > 1.5 h: amber
#endif
#ifndef CFG_FORECAST_STALE_OLD_S
#define CFG_FORECAST_STALE_OLD_S    10800UL  // > 3 h: red
#endif

/**
 * @brief Choose a daily icon based on precipitation / temperature / cloud cover.
 *
 * Wind is handled separately as a label in the UI.
 */
static DayIcon pickDayIcon(float tMax, float tMin, float precipSum, float cloudMean, int wmoCode) {
  (void)tMin;
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
static char labelNow[16];
static char labelDays[16];
static char labelToday[16];
static char labelNoWifi[24];
static char labelConnecting[24];
static char labelNoData1[16];
static char labelNoData2[16];
static char labelNoForecast[32];
static char labelNoHourly[32];
static char labelRising[16];
static char labelFalling[16];
static char labelSteady[24];
static char labelAgo[16];
static char unitMin[8];
static char unitHour[8];
static char unitDay[8];
static char unitLiters[8];
static char unitKmh[12];

/* -------------------------------------------------------------------------- */
/*                              Small helpers                                 */
/* -------------------------------------------------------------------------- */

/** @brief Text width in px for the classic 6x8 font. */
static inline int textW(const char* s, uint8_t size) {
  return (int)strlen(s) * 6 * size;
}

/** @brief Draw text with its top-left corner at (x, y). */
static void drawText(int x, int y, uint8_t size, uint16_t color, const char* s) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(x, y);
  tft.print(s);
}

/**
 * @brief Format temperature without unit as "-12.3".
 */
static void formatTemp1NoUnit(char* out, size_t outLen, float tempC) {
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
 * @brief Format temperature for the big display (integer if < -10C, else 1 decimal).
 * Keeps the value within 4 characters so it fits a 160 px column at size 6.
 */
static void formatBigTempNoUnit(char* out, size_t outLen, float tempC) {
  if (tempC < -10.0f) {
    snprintf(out, outLen, "%d", (int)roundf(tempC));
    return;
  }

  formatTemp1NoUnit(out, outLen, tempC);
}

/** @brief Format value as integer percentage "55%". */
static void formatPercent0(char* out, size_t outLen, float value) {
  snprintf(out, outLen, "%d%%", (int)roundf(value));
}

/** @brief Pick a UI color for a temperature value (C). */
static uint16_t colorForTemperature(float tempC) {
  if (tempC < 10) return CLR_SKY;
  if (tempC < 18) return CLR_COOL;
  if (tempC <= 25) return CLR_OK;
  if (tempC <= 30) return CLR_WARM;

  return CLR_HOT;
}

/** @brief 0 = fresh, 1 = getting old, 2 = old. */
static uint8_t freshnessLevel(const UiStatus& st, uint32_t warnS, uint32_t oldS) {
  if (!st.haveUpdate) return 0;
  if (st.updAgeS > oldS) return 2;
  if (st.updAgeS > warnS) return 1;
  return 0;
}

/** @brief Small trend triangle: up (amber), down (sky), steady (muted, pointing right). */
static void drawTrendTri(int x, int y, int w, int h, int8_t trend) {
  if (trend > 0) {
    tft.fillTriangle(x, y + h, x + w / 2, y, x + w, y + h, CLR_WARM);
  } else if (trend < 0) {
    tft.fillTriangle(x, y, x + w, y, x + w / 2, y + h, CLR_SKY);
  } else {
    tft.fillTriangle(x, y, x + w, y + h / 2, x, y + h, CLR_MUTED);
  }
}

/** @brief Degree ring (2 px thick). */
static void drawDegree(int cx, int cy, int r, uint16_t color) {
  tft.drawCircle(cx, cy, r, color);
  tft.drawCircle(cx, cy, r - 1, color);
}

/** @brief Water drop: point at (cx, top), round bottom of radius r. Height ~ 3r. */
static void drawDrop(int cx, int top, int r, uint16_t color) {
  const int cy = top + 2 * r;
  tft.fillTriangle(cx, top, cx - r, cy, cx + r, cy, color);
  tft.fillCircle(cx, cy, r, color);
}

/** @brief Wind pictogram: three strokes, 12x10. */
static void drawWindIcon(int x, int y, uint16_t color) {
  tft.fillRect(x, y + 1, 8, 2, color);
  tft.fillRect(x + 8, y, 2, 2, color);
  tft.fillRect(x, y + 4, 11, 2, color);
  tft.fillRect(x, y + 7, 6, 2, color);
  tft.fillRect(x + 6, y + 8, 2, 2, color);
}

/** @brief "Outside" pictogram: fir tree, 20x22. */
static void drawTreeIcon(int x, int y, uint16_t color) {
  tft.fillTriangle(x + 10, y, x + 3, y + 9, x + 17, y + 9, color);
  tft.fillTriangle(x + 10, y + 4, x, y + 17, x + 20, y + 17, color);
  tft.fillRect(x + 8, y + 17, 4, 5, color);
}

/** @brief "Inside" pictogram: house, 22x21. */
static void drawHouseIcon(int x, int y, uint16_t color) {
  tft.fillTriangle(x + 11, y, x, y + 10, x + 22, y + 10, color);
  tft.fillRect(x + 3, y + 10, 17, 11, color);
  tft.fillRect(x + 9, y + 14, 4, 7, CLR_BLACK);
}

/** @brief Circular "refresh" arrow, ~12x12 around (cx, cy). Drawn on the status bar. */
static void drawRefreshIcon(int cx, int cy, uint16_t color) {
  tft.drawCircle(cx, cy, 5, color);
  tft.drawCircle(cx, cy, 4, color);
  tft.fillRect(cx + 1, cy - 6, 6, 5, CLR_STATUS_BG);          // open the top-right quarter
  tft.fillTriangle(cx, cy - 8, cx + 4, cy - 5, cx, cy - 2, color);  // arrow head
}

/** @brief Format "ПРЕДИ 45МИН" / "ПРЕДИ 2Ч" / "ПРЕДИ 3Д". */
static void formatAgo(char* out, size_t outLen, uint32_t ageS) {
  const uint32_t mins = ageS / 60UL;
  if (mins < 60UL) {
    snprintf(out, outLen, "%s%lu%s", labelAgo, (unsigned long)mins, unitMin);
  } else if (mins < 48UL * 60UL) {
    snprintf(out, outLen, "%s%lu%s", labelAgo, (unsigned long)(mins / 60UL), unitHour);
  } else {
    snprintf(out, outLen, "%s%lu%s", labelAgo, (unsigned long)(mins / 1440UL), unitDay);
  }
}

/* -------------------------------------------------------------------------- */
/*                               Status bar                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Draw the top status bar (y 0..22).
 * @param title  Screen name shown in the middle (already converted with utf8rus).
 * @param warnS  Age (s) after which the update time turns amber.
 * @param oldS   Age (s) after which the update time turns red.
 */
static void drawStatusBar(const UiStatus& st, const char* title, uint32_t warnS, uint32_t oldS) {
  tft.fillRect(0, 0, tft.width(), 22, CLR_STATUS_BG);
  tft.drawFastHLine(0, 22, tft.width(), CLR_DIVIDER);

  // WiFi bars (x 8..23)
  uint8_t bars     = 0;
  uint16_t barClr  = CLR_OK;
  if (st.wifi == WIFI_UI_OK) {
    if (st.rssi >= -60) bars = 4;
    else if (st.rssi >= -67) bars = 3;
    else if (st.rssi >= -75) bars = 2;
    else bars = 1;
    if (bars <= 2) barClr = CLR_WARM;
  }
  static const uint8_t barH[4] = {4, 7, 10, 13};
  for (uint8_t i = 0; i < 4; i++) {
    tft.fillRect(8 + i * 4, 16 - barH[i], 3, barH[i], (i < bars) ? barClr : CLR_TRACK);
  }
  if (st.wifi == WIFI_UI_DOWN) {
    tft.drawLine(9, 4, 22, 15, CLR_HOT);
    tft.drawLine(10, 4, 23, 15, CLR_HOT);
    tft.drawLine(22, 4, 9, 15, CLR_HOT);
    tft.drawLine(23, 4, 10, 15, CLR_HOT);
  }

  // Middle: screen name, or the WiFi problem
  if (st.wifi == WIFI_UI_DOWN) {
    drawCenteredText(tft, labelNoWifi, tft.width() / 2, 4, 2, CLR_HOT);
  } else if (st.wifi == WIFI_UI_CONNECTING) {
    drawCenteredText(tft, labelConnecting, tft.width() / 2, 4, 2, CLR_WARM);
  } else {
    drawCenteredText(tft, title, tft.width() / 2, 4, 2, CLR_MUTED);
  }

  // Right: time of the last successful update, colored by age
  uint16_t clr = CLR_CLOUD;
  if (!st.haveUpdate) {
    clr = CLR_DIM;
  } else {
    const uint8_t f = freshnessLevel(st, warnS, oldS);
    if (f == 1) clr = CLR_WARM;
    else if (f == 2) clr = CLR_HOT;
  }
  drawRefreshIcon(240, 11, clr);

  char buf[8];
  if (st.haveUpdate && st.updTimeValid) {
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)st.updHour, (unsigned)st.updMin);
  } else {
    strcpy(buf, "--:--");
  }
  drawText(252, 4, 2, clr, buf);
}

/* -------------------------------------------------------------------------- */
/*                               Main screen                                  */
/* -------------------------------------------------------------------------- */

/** @brief Pressure block: value, trend word, 970..1050 scale with marker. */
static void drawPressureBlock(float pressure, int8_t trend, bool stale, uint32_t ageS) {
  const uint16_t valClr  = stale ? CLR_DIM : CLR_WHITE;
  const uint16_t softClr = stale ? CLR_DIM : CLR_MUTED;

  // Value "1015.6" + "hPa"
  const int p10   = (int)roundf(pressure * 10.0f);
  const int whole = p10 / 10;
  const int frac  = (p10 < 0) ? -(p10 % 10) : (p10 % 10);
  char pStr[16];
  snprintf(pStr, sizeof(pStr), "%d.%d", whole, frac);
  drawText(16, 142, 3, valClr, pStr);
  drawText(16 + textW(pStr, 3) + 6, 149, 2, softClr, "hPa");

  // Right side: trend word (or how old the data is)
  if (stale) {
    char ago[32];
    formatAgo(ago, sizeof(ago), ageS);
    drawRightAlignedText(tft, ago, 304, 147, 2, CLR_HOT);
  } else {
    const char* word = labelSteady;
    uint16_t wordClr = CLR_CLOUD;
    if (trend > 0) { word = labelRising;  wordClr = CLR_WARM; }
    if (trend < 0) { word = labelFalling; wordClr = CLR_SKY; }
    const int wordX = 304 - textW(word, 2);
    drawText(wordX, 147, 2, wordClr, word);
    drawTrendTri(wordX - 14, 149, 10, 12, trend);
  }

  // Scale 970..1050 hPa over x 16..304
  const int x0 = 16;
  const int w  = 288;
  tft.fillRoundRect(x0, 193, w, 4, 2, CLR_TRACK);
  const int normX = x0 + (int)((1013.0f - 970.0f) * w / 80.0f);
  tft.drawFastVLine(normX, 189, 12, softClr);

  int px = x0 + (int)((pressure - 970.0f) * w / 80.0f);
  px = constrain(px, x0, x0 + w);
  tft.fillTriangle(px - 7, 181, px + 7, 181, px, 191, valClr);

  drawText(x0, 206, 2, softClr, "970");
  drawCenteredText(tft, "1013", normX, 206, 2, softClr);
  drawRightAlignedText(tft, "1050", x0 + w, 206, 2, softClr);
}

/** @brief Render the main readings screen. */
void drawMainScreen(const UiStatus& st) {
  tft.fillScreen(CLR_BLACK);
  drawStatusBar(st, labelNow, CFG_GAUGE_STALE_WARN_S, CFG_GAUGE_STALE_OLD_S);

  tft.drawFastVLine(160, 23, 107, CLR_DIVIDER);
  tft.drawFastHLine(0, 130, tft.width(), CLR_DIVIDER);

  const bool extStale = haveExtData && freshnessLevel(st, CFG_GAUGE_STALE_WARN_S, CFG_GAUGE_STALE_OLD_S) == 2;

  // ---- Outside (left column) ----
  drawTreeIcon(4, 93, CLR_CLOUD);
  if (haveExtData) {
    char tStr[16];
    formatBigTempNoUnit(tStr, sizeof(tStr), extTemperature);
    drawRightAlignedText(tft, tStr, 148, 34, 6, extStale ? CLR_DIM : colorForTemperature(extTemperature));
    if (!extStale) drawTrendTri(150, 66, 8, 10, extTempTrend);

    char hStr[10];
    formatPercent0(hStr, sizeof(hStr), extHumidity);
    const int hx = 140 - textW(hStr, 4);
    drawDrop(hx - 10, 94, 6, extStale ? CLR_SKY_DIM : CLR_SKY);
    drawText(hx, 90, 4, extStale ? CLR_DIM : CLR_WHITE, hStr);
    if (!extStale) drawTrendTri(143, 100, 8, 10, extHumTrend);
  } else {
    drawCenteredText(tft, labelNoData1, 80, 36, 3, CLR_WARM);
    drawCenteredText(tft, labelNoData2, 80, 64, 3, CLR_WARM);
  }

  // ---- Inside (right column) ----
  drawHouseIcon(164, 94, CLR_CLOUD);
  if (haveIntData) {
    char tStr[16];
    formatTemp1NoUnit(tStr, sizeof(tStr), intTemperature);
    drawCenteredText(tft, tStr, 240, 34, 6, colorForTemperature(intTemperature));

    char hStr[10];
    formatPercent0(hStr, sizeof(hStr), intHumidity);
    const int hx = 310 - textW(hStr, 4);
    drawDrop(hx - 10, 94, 6, CLR_SKY);
    drawText(hx, 90, 4, CLR_WHITE, hStr);
  } else {
    drawCenteredText(tft, labelNoData1, 240, 36, 3, CLR_WARM);
    drawCenteredText(tft, labelNoData2, 240, 64, 3, CLR_WARM);
  }

  // ---- Pressure ----
  if (haveExtData) drawPressureBlock(extPressure, extPressTrend, extStale, st.updAgeS);

}

/* -------------------------------------------------------------------------- */
/*                        Forecast column (shared)                            */
/* -------------------------------------------------------------------------- */

/** @brief Max temp (size 3, colored) + degree ring, centered on cx at y=118. */
static void drawMaxTemp(int cx, float tMax, uint16_t color) {
  char s[8];
  snprintf(s, sizeof(s), "%d", (int)roundf(tMax));
  const int w = textW(s, 3);
  const int x = cx - (w + 10) / 2;
  drawText(x, 118, 3, color, s);
  drawDegree(x + w + 6, 121, 3, color);
}

/** @brief Min temp (size 2) + degree ring, centered on cx at y=146. */
static void drawMinTemp(int cx, float tMin, uint16_t color) {
  char s[8];
  snprintf(s, sizeof(s), "%d", (int)roundf(tMin));
  const int w = textW(s, 2);
  const int x = cx - (w + 8) / 2;
  drawText(x, 146, 2, color, s);
  tft.drawCircle(x + w + 4, 148, 2, color);
}

/* -------------------------------------------------------------------------- */
/*                            3-day forecast                                  */
/* -------------------------------------------------------------------------- */

/** @brief Render the forecast screen. */
void drawForecastScreen(const UiStatus& st) {
  tft.fillScreen(CLR_BLACK);
  drawStatusBar(st, labelDays, CFG_FORECAST_STALE_WARN_S, CFG_FORECAST_STALE_OLD_S);

  if (forecastCount == 0) {
    drawCenteredText(tft, labelNoForecast, tft.width() / 2, 112, 2, CLR_WARM);
    return;
  }

  const int colW = tft.width() / 3;  // 106

  for (int i = 1; i < 3; i++) {
    tft.drawFastVLine(i * colW, 23, 217, CLR_DIVIDER);
  }
  tft.drawFastHLine(0, 170, tft.width(), CLR_DIVIDER);

  for (int i = 0; i < forecastCount && i < 3; i++) {
    const ForecastDay& d = forecast[i];
    const int x0 = i * colW;
    const int cx = x0 + colW / 2;

    char dayLabel[12];
    formatDateLabelDDMM(d.label, dayLabel, sizeof(dayLabel));
    drawCenteredText(tft, dayLabel, cx, 32, 2, CLR_SKY);

    DayIcon icon = pickDayIcon(d.tMax, d.tMin, d.precip, d.cloudMean, d.wmoCode);
    drawWeatherIcon(tft, cx, 56, icon);

    drawMaxTemp(cx, d.tMax, colorForTemperature(d.tMax));
    drawMinTemp(cx, d.tMin, CLR_CLOUD);

    // Rain
    const int rain = (int)(d.precip + 0.5f);
    char rainStr[16];
    snprintf(rainStr, sizeof(rainStr), "%d%s", rain, unitLiters);
    drawDrop(x0 + 20, 185, 4, CLR_SKY);
    drawText(x0 + 30, 184, 2, rain > 0 ? CLR_SKY : CLR_WHITE, rainStr);

    // Wind
    const int wind = (int)(d.windMax + 0.5f);
    char windStr[24];
    if (wind < 100) {
      snprintf(windStr, sizeof(windStr), "%d%s", wind, unitKmh);
    } else {
      snprintf(windStr, sizeof(windStr), "%d", wind);
    }
    drawWindIcon(x0 + 14, 213, CLR_MUTED);
    drawText(x0 + 30, 210, 2, CLR_WHITE, windStr);
  }

}

/* -------------------------------------------------------------------------- */
/*                         Today (4 x 6-hour blocks)                          */
/* -------------------------------------------------------------------------- */

/** @brief Time range labels for the 4 blocks. */
static const char* const blockLabels[4] = {"00-06", "06-12", "12-18", "18-24"};

/** @brief Render today's 6-hour forecast screen (4 columns, current block highlighted). */
void drawTodayScreen(const UiStatus& st) {
  tft.fillScreen(CLR_BLACK);
  drawStatusBar(st, labelToday, CFG_FORECAST_STALE_WARN_S, CFG_FORECAST_STALE_OLD_S);

  if (!haveTodayForecast) {
    drawCenteredText(tft, labelNoHourly, tft.width() / 2, 112, 2, CLR_WARM);
    return;
  }

  const int colW    = tft.width() / 4;                   // 80
  const int current = st.clockValid ? (st.hour / 6) : -1; // -1: unknown, no highlight

  // Highlight the current block (before grid lines so they stay on top)
  if (current >= 0) {
    tft.fillRect(current * colW + 1, 23, colW - 1, 217, CLR_STATUS_BG);
    tft.fillRect(current * colW + 10, 50, 60, 2, CLR_SKY);
  }

  for (int i = 1; i < 4; i++) {
    tft.drawFastVLine(i * colW, 23, 217, CLR_DIVIDER);
  }
  tft.drawFastHLine(0, 170, tft.width(), CLR_DIVIDER);

  for (int b = 0; b < 4; b++) {
    const ForecastBlock& fb = todayBlocks[b];
    const int x0            = b * colW;
    const int cx            = x0 + colW / 2;
    const bool isPast       = (current >= 0) && (b < current);
    const bool isNow        = (b == current);
    const uint16_t bg       = isNow ? CLR_STATUS_BG : CLR_BLACK;

    const uint16_t headClr = isPast ? CLR_DIM : (isNow ? CLR_WHITE : CLR_SKY);
    drawCenteredText(tft, blockLabels[b], cx, 32, 2, headClr);

    if (!fb.valid) {
      drawCenteredText(tft, "--", cx, 118, 3, CLR_DIM);
      continue;
    }

    DayIcon icon = pickDayIcon(fb.tMax, fb.tMin, fb.precip, fb.cloudMean, fb.wmoCode);
    drawWeatherIconSmall(tft, cx, 68, icon, bg, isPast);

    drawMaxTemp(cx, fb.tMax, isPast ? CLR_DIM : colorForTemperature(fb.tMax));
    drawMinTemp(cx, fb.tMin, isPast ? CLR_DIM : CLR_CLOUD);

    // Rain
    const int rain = (int)(fb.precip + 0.5f);
    char rainStr[16];
    snprintf(rainStr, sizeof(rainStr), "%d%s", rain, unitLiters);
    drawDrop(x0 + 14, 185, 4, isPast ? CLR_SKY_DIM : CLR_SKY);
    drawText(x0 + 22, 184, 2, isPast ? CLR_DIM : (rain > 0 ? CLR_SKY : CLR_WHITE), rainStr);

    // Wind (number only: "КМ/Ч" does not fit an 80 px column at size 2)
    char windStr[8];
    snprintf(windStr, sizeof(windStr), "%d", (int)(fb.windMax + 0.5f));
    drawWindIcon(x0 + 8, 213, isPast ? CLR_DIM : CLR_MUTED);
    drawText(x0 + 22, 210, 2, isPast ? CLR_DIM : CLR_WHITE, windStr);
  }

}

/* -------------------------------------------------------------------------- */
/*                                  Init                                      */
/* -------------------------------------------------------------------------- */

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

  // Pre-convert all Cyrillic labels once at startup (uppercase reads better on this panel).
  utf8rus("СЕГА", labelNow, sizeof(labelNow));
  utf8rus("3 ДНИ", labelDays, sizeof(labelDays));
  utf8rus("ДНЕС", labelToday, sizeof(labelToday));
  utf8rus("НЯМА WIFI", labelNoWifi, sizeof(labelNoWifi));
  utf8rus("СВЪРЗВАНЕ", labelConnecting, sizeof(labelConnecting));
  utf8rus("НЯМА", labelNoData1, sizeof(labelNoData1));
  utf8rus("ДАННИ", labelNoData2, sizeof(labelNoData2));
  utf8rus("НЯМА ПРОГНОЗА", labelNoForecast, sizeof(labelNoForecast));
  utf8rus("НЯМА ДАННИ", labelNoHourly, sizeof(labelNoHourly));
  utf8rus("РАСТЕ", labelRising, sizeof(labelRising));
  utf8rus("ПАДА", labelFalling, sizeof(labelFalling));
  utf8rus("СТАБИЛНО", labelSteady, sizeof(labelSteady));
  utf8rus("ПРЕДИ ", labelAgo, sizeof(labelAgo));
  utf8rus("МИН", unitMin, sizeof(unitMin));
  utf8rus("Ч", unitHour, sizeof(unitHour));
  utf8rus("Д", unitDay, sizeof(unitDay));
  utf8rus(" Л", unitLiters, sizeof(unitLiters));
  utf8rus("КМ/Ч", unitKmh, sizeof(unitKmh));

  initUtilLabels();
}
