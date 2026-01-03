#pragma once
#include <Arduino.h>

struct ForecastDay {
  String label;
  int wmoCode;       // WMO weather code (used only as a hint, e.g. thunderstorms)
  float tMin;
  float tMax;
  float precip;      // mm
  float windMax;     // km/h
  float cloudMean;   // %
  bool valid;
};

extern float extTemperature;
extern float extHumidity;
extern float extPressure;
extern float intTemperature;
extern float intHumidity;
extern bool haveExtData;
extern bool haveIntData;

// -1 = falling, 0 = steady/unknown, +1 = rising
extern int8_t extTempTrend;
extern int8_t extHumTrend;
extern int8_t extPressTrend;

extern ForecastDay forecast[3];
extern int forecastCount;

bool fetchGaugeData();
bool fetchForecast();
