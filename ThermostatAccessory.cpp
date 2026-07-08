#include "ThermostatAccessory.h"
#include "Config.h"
#include <math.h>

void ThermostatAccessory::begin() {
  // --- COOL + HEAT + AUTO, where AUTO is repurposed: it sends the AC's
  // fan-only command (no cooling/heating). The two-handle range the Home
  // app shows in Auto is Matter bureaucracy - fan mode ignores it. ---

  thermostat.begin(
      MatterThermostat::THERMOSTAT_SEQ_OP_COOLING_HEATING,
      MatterThermostat::THERMOSTAT_AUTO_MODE_ENABLED);

  // Setpoints/mode are NOT set here: value writes made before Matter.begin()
  // get clobbered by server init. The sketch calls applyDefaults() or
  // syncFromDevice() after Matter.begin() instead. Attribute CREATION is the
  // opposite - it must happen before the server snapshots the data model.
  applySetpointLimits();

  // Initialize fan control with OFF, LOW, MEDIUM, HIGH modes
  fan.begin(0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_LOW_MED_HIGH);
  
  Serial.println("[Thermostat] Initialized with fan control (OFF/LOW/MED/HIGH modes)");

  // ---- Mode changed by the controller ----
  thermostat.onChangeMode([this](MatterThermostat::ThermostatMode_t newMode) {
    Serial.printf("[Thermostat] Mode → %s\n",
                  MatterThermostat::getThermostatModeString(newMode));
    mode = newMode;
    applyToAC();
    return true;
  });

  // ---- Cooling setpoint changed by the controller ----
  thermostat.onChangeCoolingSetpoint([this](double c) {
    Serial.printf("[Thermostat] Cooling setpoint: %.1f°C\n", c);
    coolSetpoint = c;
    // Keep the pair ordered (cool >= heat): some controllers still couple
    // the two setpoints even without the AUTO feature.
    if (heatSetpoint > c) {
      pendingHeatSetpoint = c;
    }
    applyToAC();
    return true;
  });

  // ---- Heating setpoint changed by the controller ----
  thermostat.onChangeHeatingSetpoint([this](double h) {
    Serial.printf("[Thermostat] Heating setpoint: %.1f°C\n", h);
    heatSetpoint = h;
    // Same in the other direction: drag cooling up with heating.
    if (coolSetpoint < h) {
      pendingCoolSetpoint = h;
    }
    applyToAC();
    return true;
  });

  // ---- Callback when fan speed changes ----
  fan.onChangeSpeedPercent([this](uint8_t speedPercent) {
    Serial.printf("[Fan] Speed changed to: %d%%\n", speedPercent);
    if (ir != nullptr && !mirroringFromIR) {
      ir->sendFanCommand(speedPercent, true);
    }
    return true;
  });

  // ---- Callback when fan mode changes ----
  fan.onChangeMode([this](MatterFan::FanMode_t fanMode) {
    Serial.printf("[Fan] Mode changed to: %s\n", fan.getFanModeString(fanMode));
    return true;
  });
}

void ThermostatAccessory::setIRController(IRController *irController) {
  ir = irController;
  ir->onIRState([this](const stdAc::state_t &state) { mirrorIRState(state); });
}

// The user pressed buttons on the AC's own remote: reflect the new state in
// Matter so the Home app stays truthful. mirroringFromIR suppresses the
// resulting callbacks from re-sending the same state over IR.
void ThermostatAccessory::mirrorIRState(const stdAc::state_t &state) {
  mirroringFromIR = true;

  if (!state.power) {
    thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_OFF);
    mode = MatterThermostat::THERMOSTAT_MODE_OFF;
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

// Publish the AC's real range as the setpoint limits, heat and cool alike.
// Apple Home reads these (at pairing time) and caps the dial accordingly.
// The esp_matter thermostat feature does NOT create the limit attributes,
// so they must be created here - before Matter.begin() snapshots the data
// model. If a future core version starts creating them, fall back to update.
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

// Publish MinSetpointDeadBand = 0 (spec range 0..2.5C). Controllers then
// stop forcing a gap between the setpoints, so the Cool dial keeps its
// full 16..30 range even though the AUTO feature is advertised.
void ThermostatAccessory::zeroDeadband() {
  esp_matter_attr_val_t val = esp_matter_int8(0);
  esp_matter::attribute::update(thermostat.getEndPointId(),
                                chip::app::Clusters::Thermostat::Id,
                                chip::app::Clusters::Thermostat::Attributes::MinSetpointDeadBand::Id, &val);
  Serial.println("[Thermostat] Deadband set to 0");
}

// Fresh device: push our defaults into the live attribute store.
void ThermostatAccessory::applyDefaults() {
  thermostat.setLocalTemperature(22.0);
  thermostat.setCoolingHeatingSetpoints(heatSetpoint, coolSetpoint);
  thermostat.setMode(MatterThermostat::THERMOSTAT_MODE_COOL);
  mode = MatterThermostat::THERMOSTAT_MODE_COOL;
  Serial.printf("[Thermostat] Defaults applied: heat %.1f / cool %.1f, mode COOL\n",
                heatSetpoint, coolSetpoint);
}

// Commissioned device: the server restored persisted values at startup -
// read them so our shadow state (and the AC) match what the app shows.
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

void ThermostatAccessory::tick() {
  if (!isnan(pendingHeatSetpoint)) {
    double h = pendingHeatSetpoint;
    pendingHeatSetpoint = NAN;
    Serial.printf("[Thermostat] Auto-adjusting heating setpoint to %.1f°C (deadband)\n", h);
    if (thermostat.setHeatingSetpoint(h)) {
      heatSetpoint = h;
    }
  }
  if (!isnan(pendingCoolSetpoint)) {
    double c = pendingCoolSetpoint;
    pendingCoolSetpoint = NAN;
    Serial.printf("[Thermostat] Auto-adjusting cooling setpoint to %.1f°C (deadband)\n", c);
    if (thermostat.setCoolingSetpoint(c)) {
      coolSetpoint = c;
    }
  }
}

void ThermostatAccessory::updateTemperature(float tempC) {
  thermostat.setLocalTemperature(tempC);
}

double ThermostatAccessory::effectiveTargetTemperature() const {
  double target = (mode == MatterThermostat::THERMOSTAT_MODE_HEAT) ? heatSetpoint : coolSetpoint;
  target = constrain(target, (double)AC_MIN_TEMP_C, (double)AC_MAX_TEMP_C);
  // ACs take targets in 0.5C steps.
  return round(target * 2.0) / 2.0;
}

void ThermostatAccessory::applyToAC() {
  // State came FROM the AC remote - it's already on the AC, don't echo it.
  if (mirroringFromIR) {
    return;
  }

  if (mode == MatterThermostat::THERMOSTAT_MODE_OFF) {
    Serial.println("[AC] → OFF");
    if (ir != nullptr) {
      ir->sendThermostatCommand(false, 0, (int)effectiveTargetTemperature());
    }
    return;
  }

  // AUTO is repurposed as fan-only: air circulation, no heating/cooling,
  // target temperature irrelevant.
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
