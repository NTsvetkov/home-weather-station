#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include "sensors.h"

static Adafruit_AHTX0 aht;

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
  temperature = temp_event.temperature;
  humidity = humidity_event.relative_humidity;
  return true;
}
