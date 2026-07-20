#include "ThermostatAccessory.h"
#include "Config.h"
#include <math.h>

void ThermostatAccessory::begin() {
  // AUTO is repurposed as fan-only; its two-handle range UI is ignored
  thermostat.begin(
      MatterThermostat::THERMOSTAT_SEQ_OP_COOLING_HEATING,
      MatterThermostat::THERMOSTAT_AUTO_MODE_ENABLED);

  // Attribute creation must happen before Matter.begin() snapshots the model
  applySetpointLimits();

  fan.begin(0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_LOW_MED_HIGH);
  humidity.begin(50.0);

  Serial.println("[Thermostat] Initialized with fan control (OFF/LOW/MED/HIGH modes)");

  thermostat.onChangeMode([this](MatterThermostat::ThermostatMode_t newMode) {
    Serial.printf("[Thermostat] Mode → %s\n",
                  MatterThermostat::getThermostatModeString(newMode));
    mode = newMode;
    // IR only from the loop task (tick); mode taps send without debounce
    if (!mirroringFromIR) {
      applyDueAt = millis();
      if (applyDueAt == 0) {
        applyDueAt = 1;
      }
    }
    // Park the inactive setpoint, restore the active one
    if (newMode == MatterThermostat::THERMOSTAT_MODE_COOL) {
      pendingCoolAttr = coolSetpoint;
      pendingHeatAttr = AC_MIN_TEMP_C;
    } else if (newMode == MatterThermostat::THERMOSTAT_MODE_HEAT) {
      pendingHeatAttr = heatSetpoint;
      pendingCoolAttr = AC_MAX_TEMP_C;
    }
    return true;
  });

  thermostat.onChangeCoolingSetpoint([this](double c) {
    if (internalWrite) {
      return true;
    }
    Serial.printf("[Thermostat] Cooling setpoint: %.1f°C\n", c);
    coolSetpoint = c;
    scheduleApplyToAC();
    return true;
  });

  thermostat.onChangeHeatingSetpoint([this](double h) {
    if (internalWrite) {
      return true;
    }
    Serial.printf("[Thermostat] Heating setpoint: %.1f°C\n", h);
    heatSetpoint = h;
    scheduleApplyToAC();
    return true;
  });

  fan.onChangeSpeedPercent([this](uint8_t speedPercent) {
    Serial.printf("[Fan] Speed changed to: %d%%\n", speedPercent);
    if (mirroringFromIR || internalWrite) {
      return true;
    }
    // No fan to control while the AC is off - revert the change
    if (mode == MatterThermostat::THERMOSTAT_MODE_OFF) {
      if (speedPercent > 0) {
        Serial.println("[Fan] Ignored - thermostat is off");
        fanRevertPending = true;
      }
      return true;
    }
    pendingFanPercent = speedPercent;
    return true;
  });

  fan.onChangeMode([this](MatterFan::FanMode_t fanMode) {
    Serial.printf("[Fan] Mode changed to: %s\n", fan.getFanModeString(fanMode));
    if (mirroringFromIR || internalWrite) {
      return true;
    }
    if (mode == MatterThermostat::THERMOSTAT_MODE_OFF && fanMode != MatterFan::FAN_MODE_OFF) {
      Serial.println("[Fan] Ignored - thermostat is off");
      fanRevertPending = true;
    }
    return true;
  });
}

void ThermostatAccessory::setIRController(IRController *irController) {
  ir = irController;
  ir->onIRState([this](const stdAc::state_t &state) { mirrorIRState(state); });
}

// AC remote was used: reflect the new state in Matter (no IR echo)
void ThermostatAccessory::mirrorIRState(const stdAc::state_t &state) {
  mirroringFromIR = true;

  if (!state.power) {
    thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_OFF);
    mode = MatterThermostat::THERMOSTAT_MODE_OFF;
    if (fan.getMode() != MatterFan::FAN_MODE_OFF) {
      savedFanPercent = fan.getSpeedPercent();
      fan.setMode(MatterFan::FAN_MODE_OFF);
    }
  } else {
    switch (state.mode) {
      case stdAc::opmode_t::kHeat:
        thermostat.setHeatingSetpoint(state.degrees);
        heatSetpoint = state.degrees;
        thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_HEAT);
        mode = MatterThermostat::THERMOSTAT_MODE_HEAT;
        break;
      case stdAc::opmode_t::kCool:
        thermostat.setCoolingSetpoint(state.degrees);
        coolSetpoint = state.degrees;
        thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_COOL);
        mode = MatterThermostat::THERMOSTAT_MODE_COOL;
        break;
      case AUTO_MODE:
        thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_AUTO);
        mode = MatterThermostat::THERMOSTAT_MODE_AUTO;
        break;
      default:
        break;
    }
  }

  int fanPercent = 0;
  switch (state.fanspeed) {
    case stdAc::fanspeed_t::kLow:    fanPercent = 33; break;
    case stdAc::fanspeed_t::kMedium: fanPercent = 66; break;
    case stdAc::fanspeed_t::kHigh:   fanPercent = 99; break;
    default:                         fanPercent = 0;  break;
  }
  fan.setSpeedPercent(fanPercent);

  Serial.println("[Thermostat] Matter state mirrored from the AC remote");
  mirroringFromIR = false;
}

// Publish the AC's real range as setpoint limits (Apple reads them at pairing)
void ThermostatAccessory::applySetpointLimits() {
  using namespace esp_matter;
  namespace TAttr = chip::app::Clusters::Thermostat::Attributes;
  namespace TCreate = esp_matter::cluster::thermostat::attribute;
  const int16_t minRaw = AC_MIN_TEMP_C * 100;
  const int16_t maxRaw = AC_MAX_TEMP_C * 100;

  endpoint_t *ep = endpoint::get(node::get(), thermostat.getEndPointId());
  if (ep == nullptr) {
    return;
  }
  cluster_t *cl = cluster::get(ep, chip::app::Clusters::Thermostat::Id);
  if (cl == nullptr) {
    return;
  }

  struct Limit {
    uint32_t id;
    attribute_t *(*create)(cluster_t *, int16_t);
    int16_t value;
  };
  const Limit limits[] = {
      {TAttr::AbsMinHeatSetpointLimit::Id, TCreate::create_abs_min_heat_setpoint_limit, minRaw},
      {TAttr::AbsMaxHeatSetpointLimit::Id, TCreate::create_abs_max_heat_setpoint_limit, maxRaw},
      {TAttr::AbsMinCoolSetpointLimit::Id, TCreate::create_abs_min_cool_setpoint_limit, minRaw},
      {TAttr::AbsMaxCoolSetpointLimit::Id, TCreate::create_abs_max_cool_setpoint_limit, maxRaw},
      {TAttr::MinHeatSetpointLimit::Id, TCreate::create_min_heat_setpoint_limit, minRaw},
      {TAttr::MaxHeatSetpointLimit::Id, TCreate::create_max_heat_setpoint_limit, maxRaw},
      {TAttr::MinCoolSetpointLimit::Id, TCreate::create_min_cool_setpoint_limit, minRaw},
      {TAttr::MaxCoolSetpointLimit::Id, TCreate::create_max_cool_setpoint_limit, maxRaw},
  };

  for (const Limit &l : limits) {
    if (attribute::get(cl, l.id) == nullptr) {
      if (l.create(cl, l.value) == nullptr) {
        Serial.printf("[Thermostat] Failed to create setpoint limit attr 0x%04lX\n", (unsigned long)l.id);
      }
    } else {
      esp_matter_attr_val_t val = esp_matter_int16(l.value);
      attribute::update(thermostat.getEndPointId(), chip::app::Clusters::Thermostat::Id, l.id, &val);
    }
  }
  Serial.printf("[Thermostat] Setpoint limits published: %d..%d C\n", AC_MIN_TEMP_C, AC_MAX_TEMP_C);
}

void ThermostatAccessory::zeroDeadband() {
  esp_matter_attr_val_t val = esp_matter_int8(0);
  esp_matter::attribute::update(thermostat.getEndPointId(),
                                chip::app::Clusters::Thermostat::Id,
                                chip::app::Clusters::Thermostat::Attributes::MinSetpointDeadBand::Id, &val);
  Serial.println("[Thermostat] Deadband set to 0");
}

void ThermostatAccessory::applyDefaults() {
  thermostat.setLocalTemperature(22.0);
  thermostat.setCoolingHeatingSetpoints(heatSetpoint, coolSetpoint);
  thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_COOL);
  mode = MatterThermostat::THERMOSTAT_MODE_COOL;
  Serial.printf("[Thermostat] Defaults applied: heat %.1f / cool %.1f, mode COOL\n",
                heatSetpoint, coolSetpoint);
}

// Adopt the server's persisted values so shadows and the AC match the app
void ThermostatAccessory::syncFromDevice() {
  esp_matter_attr_val_t val = esp_matter_invalid(NULL);
  if (thermostat.getAttributeVal(chip::app::Clusters::Thermostat::Id,
                                 chip::app::Clusters::Thermostat::Attributes::OccupiedHeatingSetpoint::Id, &val)) {
    heatSetpoint = val.val.i16 / 100.0;
  }
  if (thermostat.getAttributeVal(chip::app::Clusters::Thermostat::Id,
                                 chip::app::Clusters::Thermostat::Attributes::OccupiedCoolingSetpoint::Id, &val)) {
    coolSetpoint = val.val.i16 / 100.0;
  }
  if (thermostat.getAttributeVal(chip::app::Clusters::Thermostat::Id,
                                 chip::app::Clusters::Thermostat::Attributes::SystemMode::Id, &val)) {
    mode = (MatterThermostat::ThermostatMode_t)val.val.u8;
  }
  Serial.printf("[Thermostat] Synced from device: heat %.1f / cool %.1f, mode %s\n",
                heatSetpoint, coolSetpoint, MatterThermostat::getThermostatModeString(mode));
  applyToAC();
}

void ThermostatAccessory::scheduleApplyToAC() {
  if (mirroringFromIR) {
    return;
  }
  applyDueAt = millis() + AC_SEND_DEBOUNCE_MS;
  if (applyDueAt == 0) {
    applyDueAt = 1;  // 0 means "not scheduled"
  }
}

void ThermostatAccessory::tick() {
  // Signed difference: immune to millis() wrap
  if (applyDueAt != 0 && (long)(millis() - applyDueAt) >= 0) {
    applyDueAt = 0;
    applyToAC();
  }

  if (pendingFanPercent >= 0) {
    int percent = pendingFanPercent;
    pendingFanPercent = -1;
    if (ir != nullptr) {
      ir->sendFanCommand(percent, true);
    }
  }

  if (fanRevertPending) {
    fanRevertPending = false;
    internalWrite = true;
    fan.setSpeedPercent(0);
    fan.setMode(MatterFan::FAN_MODE_OFF);
    internalWrite = false;
  }

  // Parking/restore writes; heating first so cool >= heat holds throughout
  if (!isnan(pendingHeatAttr)) {
    double h = pendingHeatAttr;
    pendingHeatAttr = NAN;
    internalWrite = true;
    thermostat.setHeatingSetpoint(h);
    internalWrite = false;
  }
  if (!isnan(pendingCoolAttr)) {
    double c = pendingCoolAttr;
    pendingCoolAttr = NAN;
    internalWrite = true;
    thermostat.setCoolingSetpoint(c);
    internalWrite = false;
  }
}

void ThermostatAccessory::updateTemperature(float tempC) {
  thermostat.setLocalTemperature(tempC);
}

void ThermostatAccessory::updateHumidity(float humidityPercent) {
  humidity.setHumidity(humidityPercent);
}

double ThermostatAccessory::effectiveTargetTemperature() const {
  double target = (mode == MatterThermostat::THERMOSTAT_MODE_HEAT) ? heatSetpoint : coolSetpoint;
  target = constrain(target, (double)AC_MIN_TEMP_C, (double)AC_MAX_TEMP_C);
  return round(target * 2.0) / 2.0;
}

void ThermostatAccessory::applyToAC() {
  if (mirroringFromIR) {
    return;
  }
  applyDueAt = 0;  // this send carries the full state

  if (mode == MatterThermostat::THERMOSTAT_MODE_OFF) {
    Serial.println("[AC] → OFF");
    if (ir != nullptr) {
      ir->sendThermostatCommand(false, 0, (int)effectiveTargetTemperature());
    }
    // AC off stops the fan - reflect it on the fan endpoint
    if (fan.getMode() != MatterFan::FAN_MODE_OFF) {
      savedFanPercent = fan.getSpeedPercent();
      internalWrite = true;
      fan.setMode(MatterFan::FAN_MODE_OFF);
      internalWrite = false;
    }
    return;
  }

  // Back on: restore the fan endpoint to its pre-off speed
  if (fan.getMode() == MatterFan::FAN_MODE_OFF && savedFanPercent > 0) {
    internalWrite = true;
    fan.setSpeedPercent(savedFanPercent);
    fan.setMode(savedFanPercent > 66   ? MatterFan::FAN_MODE_HIGH
                : savedFanPercent > 33 ? MatterFan::FAN_MODE_MEDIUM
                                       : MatterFan::FAN_MODE_LOW);
    internalWrite = false;
    savedFanPercent = 0;
  }

  if (mode == MatterThermostat::THERMOSTAT_MODE_AUTO) {
    Serial.println("[AC] → FAN ONLY");
    if (ir != nullptr) {
      ir->sendThermostatCommand(true, 3, (int)effectiveTargetTemperature());
    }
    return;
  }

  bool heat = (mode == MatterThermostat::THERMOSTAT_MODE_HEAT);
  Serial.printf("[AC] → mode %s, target %.1f°C\n",
                heat ? "HEAT" : "COOL", effectiveTargetTemperature());
  if (ir != nullptr) {
    ir->sendThermostatCommand(true, heat ? 1 : 2, (int)effectiveTargetTemperature());
  }
}
