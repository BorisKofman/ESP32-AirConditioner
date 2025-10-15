#include "ThermostatAccessory.h"
#include <math.h>

void ThermostatAccessory::begin() {
  // Initialize Matter thermostat endpoint
  thermostat.begin(MatterThermostat::THERMOSTAT_SEQ_OP_COOLING,
                   MatterThermostat::THERMOSTAT_AUTO_MODE_ENABLED);

  // Set initial state and attributes before callbacks
  thermostat.setLocalTemperature(22.0f);
  thermostat.setCoolingHeatingSetpoints(20.0, 23.0);
  thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_AUTO);

  // ---- Mode change callback ----
  thermostat.onChangeMode([this](MatterThermostat::ThermostatMode_t mode) {
    const bool power = (mode != MatterThermostat::THERMOSTAT_MODE_OFF);
    const int mid = (thermostat.getCoolingSetpoint() + thermostat.getHeatingSetpoint()) / 2;

    Serial.printf("[Thermostat] Mode changed: %d (Power=%s)\n",
                  mode, power ? "ON" : "OFF");

    switch (mode) {
      case MatterThermostat::THERMOSTAT_MODE_COOL:
        Serial.printf("Cooling to %.1f°C\n", thermostat.getCoolingSetpoint());
        break;

      case MatterThermostat::THERMOSTAT_MODE_HEAT:
        Serial.printf("Heating to %.1f°C\n", thermostat.getHeatingSetpoint());
        break;

      case MatterThermostat::THERMOSTAT_MODE_AUTO:
        Serial.printf("Auto mode, target mid temp %.1f°C\n", (float)mid);
        break;

      case MatterThermostat::THERMOSTAT_MODE_FAN_ONLY:
        Serial.printf("Fan-only mode\n");
        break;

      default:
        Serial.println("Thermostat off");
        break;
    }
    return true;
  });

  // ---- Cooling setpoint callback ----
  thermostat.onChangeCoolingSetpoint([this](float c) {
    Serial.printf("[Thermostat] Cooling setpoint changed: %.1f°C\n", c);
    return true;
  });

  // ---- Heating setpoint callback ----
  thermostat.onChangeHeatingSetpoint([this](float c) {
    Serial.printf("[Thermostat] Heating setpoint changed: %.1f°C\n", c);
    return true;
  });
}

void ThermostatAccessory::updateTemperature(float tempC, float /*humidity*/) {
  if (isnan(lastTemp) || fabsf(tempC - lastTemp) >= 0.1f) {
    thermostat.setLocalTemperature(tempC);
    Serial.printf("[Thermostat] Current temperature: %.1f°C\n", tempC);
    lastTemp = tempC;
  }

  const float coolSP = thermostat.getCoolingSetpoint();
  const float heatSP = thermostat.getHeatingSetpoint();
  const auto mode = thermostat.getMode();

  bool heat = false, cool = false;

  if (mode == MatterThermostat::THERMOSTAT_MODE_AUTO) {
    heat = tempC < heatSP;
    cool = tempC > coolSP;
  } else if (mode == MatterThermostat::THERMOSTAT_MODE_HEAT) {
    heat = tempC < heatSP;
  } else if (mode == MatterThermostat::THERMOSTAT_MODE_COOL) {
    cool = tempC > coolSP;
  }

  if (heat)
    Serial.println("[Thermostat] Heating ON");
  else if (cool)
    Serial.println("[Thermostat] Cooling ON");
  else
    Serial.println("[Thermostat] System idle");
}
