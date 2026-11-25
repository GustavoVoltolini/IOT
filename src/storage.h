// Tudo que precisa sobreviver a um reinício, salvo na NVS (flash) do ESP32.
#pragma once

#include <Arduino.h>

#include "config.h"
#include "feeder_core.h"

struct Settings {
  float bowlMaxG = DEFAULT_BOWL_MAX_G;
  float marginG = DEFAULT_MARGIN_G;
  float portionG = DEFAULT_PORTION_G;
  float flowGps = DEFAULT_FLOW_GPS;
  long scaleOffset = CALIBRATION_OFFSET;
  float scaleFactor = CALIBRATION_FACTOR;
  bool calibrated = false;  // virou true depois de um `c<peso>`
  feeder::Meal meals[MAX_MEALS] = {};
  uint8_t mealCount = 0;
};

namespace storage {

void load(Settings& s);
void saveSettings(const Settings& s);  // capacidade, margem, porção, vazão
void saveCalibration(const Settings& s);
void saveMeals(const Settings& s);
void wipe();

}  // namespace storage
