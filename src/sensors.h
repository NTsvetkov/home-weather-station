#pragma once

#include <Arduino.h>

/**
 * @file sensors.h
 * @brief Internal sensor initialization and readings.
 */

/**
 * @brief Initialize the internal sensors (AHT20).
 * @return true if sensor was detected and initialized.
 */
bool initSensors();

/**
 * @brief Read temperature/humidity from the internal sensor.
 * @param[out] temperature Temperature in Celsius.
 * @param[out] humidity Relative humidity in percent.
 * @return true if values are valid.
 */
bool readInternalSensor(float& temperature, float& humidity);
