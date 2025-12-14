#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "display.h"
#include "data.h"

#define TFT_CS    D8
#define TFT_RST   D3
#define TFT_DC    D4

static Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

String utf8rus(String source) {
  int i, k;
  String target;
  target.reserve(source.length()); // avoid repeated allocations on small heap
  unsigned char n;
  char m[2] = { '0', '\0' };

  k = source.length(); i = 0;

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

uint16_t colorForTemperature(float tempC) {
  if (tempC < 10) return ILI9341_CYAN;
  if (tempC < 18) return ILI9341_BLUE;
  if (tempC <= 25) return ILI9341_GREEN;
  if (tempC <= 30) return ILI9341_ORANGE;
  return ILI9341_RED;
}

uint16_t colorForHumidity(float humidity) {
  if (humidity < 30) return ILI9341_CYAN;
  if (humidity < 40) return ILI9341_BLUE;
  if (humidity <= 60) return ILI9341_GREEN;
  if (humidity <= 70) return ILI9341_ORANGE;
  return ILI9341_RED;
}

String labelForDate(const String& dateStr) {
  if (dateStr.length() == 10) return dateStr.substring(8); // only day
  return dateStr;
}

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

void drawPressureBar(int x, int y, float pressure) {
  int barWidthMax = tft.width() - (2 * x);
  if (barWidthMax < 50) barWidthMax = tft.width() - 40;

  tft.drawRect(x, y, barWidthMax, 20, ILI9341_WHITE);

  int barWidth = map(pressure, 970, 1050, 0, barWidthMax);
  barWidth = constrain(barWidth, 0, barWidthMax);
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
}

void drawWeatherIcon(int centerX, int topY, int code) {
  int baseY = topY + 30;
  tft.fillRect(centerX - 50, topY, 100, 70, ILI9341_BLACK);

  bool isThunder = code == 95 || code == 96 || code == 99;
  bool isSnow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  bool isRain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || (code >= 61 && code <= 65);
  bool isFog = code == 45 || code == 48;
  bool isClear = code == 0;
  bool isCloudy = (code >= 1 && code <= 3);

  // sun
  if (isClear) {
    tft.fillCircle(centerX, baseY - 6, 18, ILI9341_YELLOW);
    for (int i = 0; i < 8; i++) {
      float angle = i * PI / 4;
      int x1 = centerX + cos(angle) * 24;
      int y1 = baseY - 6 + sin(angle) * 24;
      int x2 = centerX + cos(angle) * 32;
      int y2 = baseY - 6 + sin(angle) * 32;
      tft.drawLine(x1, y1, x2, y2, ILI9341_YELLOW);
    }
    return;
  }

  // cloud base
  if (isCloudy || isRain || isSnow || isFog || isThunder) {
    tft.fillRoundRect(centerX - 36, baseY - 18, 72, 32, 10, ILI9341_LIGHTGREY);
    tft.fillCircle(centerX - 18, baseY - 18, 14, ILI9341_LIGHTGREY);
    tft.fillCircle(centerX + 12, baseY - 18, 16, ILI9341_LIGHTGREY);
  }

  if (isRain || isThunder) {
    tft.drawFastVLine(centerX - 16, baseY + 4, 14, ILI9341_CYAN);
    tft.drawFastVLine(centerX, baseY + 6, 14, ILI9341_CYAN);
    tft.drawFastVLine(centerX + 16, baseY + 4, 14, ILI9341_CYAN);
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

  if (isFog) {
    tft.drawFastHLine(centerX - 30, baseY + 2, 60, ILI9341_DARKGREY);
    tft.drawFastHLine(centerX - 30, baseY + 10, 60, ILI9341_DARKGREY);
    tft.drawFastHLine(centerX - 30, baseY + 18, 60, ILI9341_DARKGREY);
  }
}

void drawMainScreen() {
  tft.fillScreen(ILI9341_BLACK);

  const int leftCenterX = tft.width() / 4;
  const int rightCenterX = (tft.width() * 3) / 4;
  const int labelY = 8;
  const int tempY = 55;
  const int humidityY = 140;

  int midX = tft.width() / 2;
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
    drawCenteredText(String(extTemperature, 1), leftCenterX, tempY, 6, colorForTemperature(extTemperature));
    drawCenteredText(String(extHumidity, 0) + " %", leftCenterX, humidityY, 4, colorForHumidity(extHumidity));
  } else {
    drawCenteredText(utf8rus("няма данни"), leftCenterX, tempY, 3, ILI9341_YELLOW);
  }

  drawCenteredText(utf8rus("вътре"), rightCenterX, labelY, 3, ILI9341_WHITE);
  if (haveIntData) {
    drawCenteredText(String(intTemperature, 1), rightCenterX, tempY, 6, colorForTemperature(intTemperature));
    drawCenteredText(String(intHumidity, 0) + " %", rightCenterX, humidityY, 4, colorForHumidity(intHumidity));
  } else {
    drawCenteredText(utf8rus("няма данни"), rightCenterX, tempY, 3, ILI9341_YELLOW);
  }

  if (haveExtData) drawPressureBar(20, 190, extPressure);
}

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
    int colStart = i * colW;
    int colCenter = colStart + (colW / 2);
    int colRight = colStart + colW - 6; // small padding from right edge

    drawCenteredText(labelForDate(forecast[i].label), colCenter, 45, 2, ILI9341_CYAN);
    drawWeatherIcon(colCenter, 75, forecast[i].code);

    String maxStr = String(forecast[i].tMax, 1) + " C";
    String minStr = String(forecast[i].tMin, 1) + " C";
    int rainLiters = (int)(forecast[i].precip + 0.5f);
    String rainStr = String(rainLiters) + " L";

    drawRightAlignedText(maxStr, colRight, 154, 2, ILI9341_WHITE);
    drawRightAlignedText(minStr, colRight, 176, 2, ILI9341_WHITE);

    int rainY = 210;
    int dropX = colRight - 90; // move left to avoid overlapping text
    tft.fillTriangle(dropX, rainY - 12, dropX - 6, rainY - 2, dropX + 6, rainY - 2, ILI9341_BLUE);
    tft.fillCircle(dropX, rainY - 1, 4, ILI9341_BLUE);
    drawRightAlignedText(rainStr, colRight, rainY - 6, 2, ILI9341_BLUE);
  }

  // vertical separators between days
  int sepYTop = 22;
  int sepYBottom = 214;
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
