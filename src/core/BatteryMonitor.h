// BatteryMonitor.h
#pragma once

#include <Arduino.h>

namespace Battery {
  // Read voltage and return percentage (0-100)
  uint16_t readPercentage();

  // Read the battery voltage in millivolts (accounts for divider)
  uint16_t readMillivolts();

  // Read the battery voltage in volts (accounts for divider)
  double readVolts();

  // Percentage (0-100) from a millivolt value
  uint16_t percentageFromMillivolts(uint16_t millivolts);
};
