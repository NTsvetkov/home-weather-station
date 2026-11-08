#include <Arduino.h>

#include <Adafruit_AHTX0.h>

#include "debug.h"
#include "sensors.h"

/**
 * @file sensors.cpp
 * @brief AHT20 sensor initialization and readings.
 */

static Adafruit_AHTX0 aht;
static bool sensorReady = false;
static uint32_t lastInitAttemptMs = 0;

static const float AHT_TEMP_MIN     = -40.0f;
static const float AHT_TEMP_MAX     = 60.0f;
static const float AHT_HUMIDITY_MIN = 0.0f;
static const float AHT_HUMIDITY_MAX = 100.0f;

/**
 * @brief Initialize the AHT20 sensor.
 * @return true if sensor was found and initialized.
 */
bool initSensors() {
  lastInitAttemptMs = millis();
  sensorReady = aht.begin();
  if (!sensorReady) {
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
  if (!sensorReady) {
    if (millis() - lastInitAttemptMs < CFG_SENSOR_REINIT_INTERVAL_MS || !initSensors()) return false;
  }
  sensors_event_t humidityEvent{}, tempEvent{};
  if (!aht.getEvent(&humidityEvent, &tempEvent)) {
    sensorReady = false;
    lastInitAttemptMs = millis();
    return false;
  }

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
