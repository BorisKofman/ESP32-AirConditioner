#pragma once
#include "Config.h"
#include <Matter.h>

class ThermostatAccessory {
 public:
  void begin();
  void updateTemperature(float tempC, float humidity);

 private:
  MatterThermostat thermostat;
  float lastTemp = NAN;
};
