#pragma once
#include <Arduino.h>

bool initSensors();
bool readInternalSensor(float& temperature, float& humidity);
