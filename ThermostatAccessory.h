#pragma once
#include <Matter.h>
#include "IRController.h"

class ThermostatAccessory {
public:
  void begin();
  // Wire the IR transmitter: Matter changes go out as IR commands, and
  // signals from the AC's own remote are mirrored back into Matter.
  void setIRController(IRController *irController);
  // Call ONE of these right after Matter.begin() - attribute writes made
  // before the server starts get clobbered by its initialization:
  // fresh (uncommissioned) device → write our defaults;
  // already-commissioned device → adopt its persisted values.
  void applyDefaults();
  void syncFromDevice();
  // Zero the MinSetpointDeadBand attribute so controllers don't force a gap
  // between the setpoints. Call after Matter.begin().
  void zeroDeadband();
  void tick();
  void updateTemperature(float tempC);
  void updateHumidity(float humidityPercent);

private:
  // Single target temperature to send to the AC, regardless of Matter's
  // two-setpoint model: cool setpoint in COOL, heat setpoint in HEAT and
  // in AUTO (the low end of the range), rounded to 0.5C.
  double effectiveTargetTemperature() const;
  void applyToAC();
  // Create/set the setpoint limit attributes (AC_MIN_TEMP_C..AC_MAX_TEMP_C)
  // so controllers cap their dials to the AC's real range. Must run inside
  // begin(), before Matter.begin() snapshots the data model.
  void applySetpointLimits();
  // Push a state decoded from the AC's own remote into the Matter attributes
  // (without re-sending it over IR).
  void mirrorIRState(const stdAc::state_t &state);

  MatterThermostat thermostat;
  MatterFan fan;  // Fan control - shows under same device in HomeKit
  MatterHumiditySensor humidity;
  IRController *ir = nullptr;
  // True while mirrorIRState() updates Matter attributes: the resulting
  // callbacks must not send the state back out over IR (the AC already has
  // it - the command came FROM its remote).
  bool mirroringFromIR = false;

  // Shadow of the Matter state. The library only commits new values to its
  // getters *after* the onChange callbacks return, so callbacks must record
  // the fresh values here for applyToAC() to see them.
  MatterThermostat::ThermostatMode_t mode = MatterThermostat::THERMOSTAT_MODE_OFF;
  double coolSetpoint = 22.0;
  double heatSetpoint = 19.5;

  // Some controllers couple the two setpoints (cool >= heat). When the user
  // drags one past the other, we move the other one along so the app's
  // clamp never blocks the user. NAN = nothing pending.
  double pendingHeatSetpoint = NAN;
  double pendingCoolSetpoint = NAN;
};
