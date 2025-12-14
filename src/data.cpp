#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>

#include "data.h"

// External readings
float extTemperature = 0.0f;
float extHumidity = 0.0f;
float extPressure = 0.0f;
bool haveExtData = false;

// Internal readings (filled from sensors module via main)
float intTemperature = 0.0f;
float intHumidity = 0.0f;

ForecastDay forecast[3];
int forecastCount = 0;

bool fetchGaugeData() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(7000);
  if (!https.begin(client, "https://meter.ac/gs/nodes/N200/gauge.txt")) {
    Serial.println("Gauge begin() failed");
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Gauge HTTP code: %d\n", httpCode);
    https.end();
    return false;
  }

  String payload = https.getString();
  https.end();

  int commaIndex1 = payload.indexOf(',');
  int commaIndex2 = payload.indexOf(',', commaIndex1 + 1);
  int commaIndex3 = payload.indexOf(',', commaIndex2 + 1);
  int commaIndex4 = payload.indexOf(',', commaIndex3 + 1);
  int commaIndex5 = payload.indexOf(',', commaIndex4 + 1);
  int commaIndex6 = payload.indexOf(',', commaIndex5 + 1);

  if (commaIndex1 < 0 || commaIndex2 < 0 || commaIndex3 < 0 || commaIndex4 < 0 || commaIndex5 < 0 || commaIndex6 < 0) {
    Serial.println("Gauge parse failed (commas)");
    return false;
  }

  extTemperature = payload.substring(commaIndex1 + 1, commaIndex2).toFloat();
  extHumidity = payload.substring(commaIndex5 + 1, commaIndex6).toFloat();
  extPressure = payload.substring(commaIndex4 + 1, commaIndex5).toFloat();
  haveExtData = true;
  Serial.println("Gauge fetch OK");
  return true;
}

bool fetchForecast() {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(2048, 512);

  HTTPClient https;
  // Full request as provided (hourly included). We'll parse only daily parts but keep payload intact.
  const char* url = "https://api.open-meteo.com/v1/forecast?latitude=42.2&longitude=24.3333&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,wind_speed_10m_max&models=meteofrance_arpege_europe&timezone=auto";

  if (!https.begin(client, url)) {
    Serial.println("Forecast begin() failed");
    return false;
  }
  https.useHTTP10(true); // avoid chunked/gzip surprises on ESP8266
  https.addHeader("Accept-Encoding", "identity");

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Forecast HTTP code: %d\n", httpCode);
    https.end();
    return false;
  }

  // Use legacy document types to keep API simple on ESP8266; suppress v7 deprecation noise locally.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  StaticJsonDocument<256> filter;
  JsonObject dailyFilter = filter["daily"].to<JsonObject>();
  dailyFilter["time"] = true;
  dailyFilter["weather_code"] = true;
  dailyFilter["temperature_2m_max"] = true;
  dailyFilter["temperature_2m_min"] = true;
  dailyFilter["precipitation_sum"] = true;

  DynamicJsonDocument doc(9000);
  WiFiClient* stream = https.getStreamPtr();
  DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
#pragma GCC diagnostic pop
  https.end();
  if (err) {
    Serial.print("Forecast JSON error: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArray times = doc["daily"]["time"].as<JsonArray>();
  JsonArray codes = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray tMaxArr = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray tMinArr = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray precipArr = doc["daily"]["precipitation_sum"].as<JsonArray>();

  if (!times || !codes || !tMaxArr || !tMinArr || !precipArr) {
    Serial.println("Forecast arrays missing");
    return false;
  }

  Serial.printf("Forecast days available: %u\n", times.size());

  forecastCount = 0;
  for (int i = 0; i < 3; i++) forecast[i].valid = false;

  // skip index 0 (днес), вземи следващите три дни ако са налични
  for (size_t src = 1; src < times.size() && forecastCount < 3; src++) {
    const char* t = times[src];
    if (!t) continue;
    forecast[forecastCount].label = String(t);
    forecast[forecastCount].code = codes[src].as<int>();
    forecast[forecastCount].tMax = tMaxArr[src].as<float>();
    forecast[forecastCount].tMin = tMinArr[src].as<float>();
    forecast[forecastCount].precip = precipArr[src].as<float>();
    forecast[forecastCount].valid = true;
    forecastCount++;
  }

  return forecastCount > 0;
}
