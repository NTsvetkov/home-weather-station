#include <Arduino.h>

#include <Adafruit_AHTX0.h>

#include "debug.h"
#include "sensors.h"

/**
 * @file sensors.cpp
 * @brief AHT20 sensor initialization and readings.
 */

static Adafruit_AHTX0 aht;

static const float AHT_TEMP_MIN     = -40.0f;
static const float AHT_TEMP_MAX     = 60.0f;
static const float AHT_HUMIDITY_MIN = 0.0f;
static const float AHT_HUMIDITY_MAX = 100.0f;

/**
 * @brief Initialize the AHT20 sensor.
 * @return true if sensor was found and initialized.
 */
bool initSensors() {
  if (!aht.begin()) {
    LOG_E("AHT20 not found - check wiring");
    return false;
  }
  return true;
}

/**
 * @brief Read temperature and humidity from the AHT20 sensor.
 * @param[out] temperature Temperature in Celsius.
 * @param[out] humidity    Relative humidity in percent.
 * @return true if values are valid and within sensor range.
 */
bool readInternalSensor(float& temperature, float& humidity) {
  sensors_event_t humidityEvent, tempEvent;
  aht.getEvent(&humidityEvent, &tempEvent);

  const bool ok = isfinite(tempEvent.temperature) &&
                  isfinite(humidityEvent.relative_humidity) &&
                  tempEvent.temperature > AHT_TEMP_MIN &&
                  tempEvent.temperature < AHT_TEMP_MAX &&
                  humidityEvent.relative_humidity >= AHT_HUMIDITY_MIN &&
                  humidityEvent.relative_humidity <= AHT_HUMIDITY_MAX;

  if (!ok) return false;

  temperature = tempEvent.temperature;
  humidity    = humidityEvent.relative_humidity;
  return true;
}
