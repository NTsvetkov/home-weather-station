#define TFT_CS    D8
#define TFT_RST   D3
#define TFT_DC    D4

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "config.h"

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Adafruit_AHTX0 aht;

void setup() {
  Serial.begin(115200);
  Serial.println("ILI9341 and AHT20 Test!");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Свързване към WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" свързано!");

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  if (!aht.begin()) {
    Serial.println("Не може да се намери AHT20! Проверете връзките.");
  }
}

String utf8rus(String source) {
  int i, k;
  String target;
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

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (https.begin(client, "https://meter.ac/gs/nodes/N200/gauge.txt")) {
      int httpCode = https.GET();

      if (httpCode == HTTP_CODE_OK) {
        String payload = https.getString();

        // Parse the payload to extract external temperature, humidity, and pressure
        int commaIndex1 = payload.indexOf(',');
        int commaIndex2 = payload.indexOf(',', commaIndex1 + 1);
        int commaIndex3 = payload.indexOf(',', commaIndex2 + 1);
        int commaIndex4 = payload.indexOf(',', commaIndex3 + 1);
        int commaIndex5 = payload.indexOf(',', commaIndex4 + 1);
        int commaIndex6 = payload.indexOf(',', commaIndex5 + 1);

        float extTemperature = payload.substring(commaIndex1 + 1, commaIndex2).toFloat();
        float extHumidity = payload.substring(commaIndex5 + 1, commaIndex6).toFloat();
        float extPressure = payload.substring(commaIndex4 + 1, commaIndex5).toFloat();

        // Read internal sensor data
        sensors_event_t humidity_event, temp_event;
        aht.getEvent(&humidity_event, &temp_event);
        float intTemperature = temp_event.temperature;
        float intHumidity = humidity_event.relative_humidity;

        // Clear the screen
        tft.fillScreen(ILI9341_BLACK);

        const int leftCenterX = tft.width() / 4;      // ~80
        const int rightCenterX = (tft.width() * 3) / 4; // ~240
        const int labelY = 8;
        const int tempY = 55;
        const int humidityY = 140;

        // Grid lines
        int midX = tft.width() / 2;
        // Thicker dividers (2 px) for clarity; vertical stops at humidity/pressure boundary
        int verticalHeight = 175; // end right above the pressure section
        tft.drawFastVLine(midX, 0, verticalHeight, ILI9341_DARKGREY);
        tft.drawFastVLine(midX + 1, 0, verticalHeight, ILI9341_DARKGREY);
        tft.drawFastHLine(0, 35, tft.width(), ILI9341_DARKGREY);
        tft.drawFastHLine(0, 36, tft.width(), ILI9341_DARKGREY);
        tft.drawFastHLine(0, 115, tft.width(), ILI9341_DARKGREY);
        tft.drawFastHLine(0, 116, tft.width(), ILI9341_DARKGREY);
        tft.drawFastHLine(0, 175, tft.width(), ILI9341_DARKGREY);
        tft.drawFastHLine(0, 176, tft.width(), ILI9341_DARKGREY);

        // External label and temperature
        drawCenteredText(utf8rus("навън"), leftCenterX, labelY, 3, ILI9341_WHITE);
        drawCenteredText(String(extTemperature, 1), leftCenterX, tempY, 6, colorForTemperature(extTemperature));
        drawCenteredText(String(extHumidity, 0) + " %", leftCenterX, humidityY, 4, colorForHumidity(extHumidity));

        // Internal label and temperature
        drawCenteredText(utf8rus("вътре"), rightCenterX, labelY, 3, ILI9341_WHITE);
        drawCenteredText(String(intTemperature, 1), rightCenterX, tempY, 6, colorForTemperature(intTemperature));
        drawCenteredText(String(intHumidity, 0) + " %", rightCenterX, humidityY, 4, colorForHumidity(intHumidity));

        // Atmospheric pressure bar and value at the bottom
        drawPressureBar(20, 190, extPressure);
      }
      https.end();
    }
  }

  delay(60000); // Update every minute
}