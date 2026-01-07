#pragma once

#include <Arduino.h>

/**
 * @file display.h
 * @brief TFT display initialization and screen rendering.
 */

/** @brief Initialize the TFT display and any related state. */
void initDisplay();

/** @brief Render the main screen (indoor/outdoor readings). */
void drawMainScreen();

/** @brief Render the forecast screen (next days). */
void drawForecastScreen();
