// BatteryMonitor.cpp
#include "BatteryMonitor.h"
#include "esp_adc/adc_oneshot.h"

namespace {

// Battery ADC pin and global instance
constexpr uint8_t BatteryGpioPin = 0;
// Voltage divider multiplier (e.g., 2.0 for 100k/100k divider)
constexpr float BatteryDividerMultiplier = 2.0f;

}

namespace Battery {

uint16_t readPercentage()
{
  return percentageFromMillivolts(readMillivolts());
}

uint16_t readMillivolts()
{
    uint32_t mv = analogReadMilliVolts(BatteryGpioPin);
    return static_cast<uint32_t>(mv * BatteryDividerMultiplier);
}

double readVolts()
{
  return static_cast<double>(readMillivolts()) / 1000.0;
}

uint16_t percentageFromMillivolts(uint16_t millivolts)
{
  double volts = millivolts / 1000.0;
  // Polynomial derived from LiPo samples
  double y = -144.9390 * volts * volts * volts +
             1655.8629 * volts * volts -
             6158.8520 * volts +
             7501.3202;

  // Clamp to [0,100] and round
  y = max(y, 0.0);
  y = min(y, 100.0);
  y = round(y);
  return static_cast<int>(y);
}

}
