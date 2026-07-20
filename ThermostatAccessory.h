#pragma once
#include <Matter.h>
#include "IRController.h"

class ThermostatAccessory {
public:
  void begin();
  // Matter changes → IR commands; AC remote signals → mirrored into Matter
  void setIRController(IRController *irController);
  // Call one of these after Matter.begin() (earlier value writes get clobbered)
  void applyDefaults();
  void syncFromDevice();
  // Deadband 0 so controllers don't force a gap between setpoints
  void zeroDeadband();
  void tick();
  void updateTemperature(float tempC);
  void updateHumidity(float humidityPercent);

private:
  // Single AC target: cool setpoint in COOL, heat setpoint in HEAT
  double effectiveTargetTemperature() const;
  void applyToAC();
  // Debounced IR send - the app streams values while dragging the dial
  void scheduleApplyToAC();
  // Create setpoint limit attrs (dial range); must run before Matter.begin()
  void applySetpointLimits();
  // Push a state decoded from the AC remote into Matter (no IR echo)
  void mirrorIRState(const stdAc::state_t &state);

  MatterThermostat thermostat;
  MatterFan fan;
  MatterHumiditySensor humidity;
  IRController *ir = nullptr;
  // Suppresses IR echo while mirroring remote state into Matter
  bool mirroringFromIR = false;

  // Shadows: the library commits values only after callbacks return
  MatterThermostat::ThermostatMode_t mode = MatterThermostat::THERMOSTAT_MODE_OFF;
  double coolSetpoint = 22.0;
  double heatSetpoint = 19.5;

  // Mode entry parks the inactive setpoint at its extreme (controllers
  // enforce cool >= heat); user values live in the shadows. NAN = none.
  double pendingHeatAttr = NAN;
  double pendingCoolAttr = NAN;
  // Guards our own attribute writes from being treated as user changes
  bool internalWrite = false;

  // Debounced IR send deadline; 0 = not scheduled
  unsigned long applyDueAt = 0;
  // Queued fan speed for tick(); -1 = none
  int pendingFanPercent = -1;
  // Fan speed remembered while the thermostat is off
  int savedFanPercent = 0;
  // Fan changed while AC off - snap it back to OFF on next tick
  bool fanRevertPending = false;
};
