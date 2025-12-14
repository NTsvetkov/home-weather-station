#pragma once
#include <Arduino.h>

struct ForecastDay {
  String label;
  int code;
  float tMin;
  float tMax;
  float precip;
  bool valid;
};

extern float extTemperature;
extern float extHumidity;
extern float extPressure;
extern float intTemperature;
extern float intHumidity;
extern bool haveExtData;

extern ForecastDay forecast[3];
extern int forecastCount;

bool fetchGaugeData();
bool fetchForecast();
