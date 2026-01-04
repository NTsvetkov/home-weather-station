#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include "sensors.h"

static Adafruit_AHTX0 aht;

static const float AHT_TEMP_MIN     = -40.0f;
static const float AHT_TEMP_MAX     = 60.0f;
static const float AHT_HUMIDITY_MIN = 0.0f;
static const float AHT_HUMIDITY_MAX = 100.0f;

bool initSensors() {
  if (!aht.begin()) {
    Serial.println("Не може да се намери AHT20! Проверете връзките.");

    return false;
  }

  return true;
}

bool readInternalSensor(float& temperature, float& humidity) {
  sensors_event_t humidity_event, temp_event;
  aht.getEvent(&humidity_event, &temp_event);

  const bool ok = isfinite(temp_event.temperature) &&
                  isfinite(humidity_event.relative_humidity) &&
                  temp_event.temperature > AHT_TEMP_MIN && temp_event.temperature < AHT_TEMP_MAX &&
                  humidity_event.relative_humidity >= AHT_HUMIDITY_MIN && humidity_event.relative_humidity <= AHT_HUMIDITY_MAX;

  if (!ok) {
    return false;
  }

  temperature = temp_event.temperature;
  humidity    = humidity_event.relative_humidity;
  return true;
}
