#include "FanAccessory.h"

// Constructor implementation
FanAccessory::FanAccessory(IRController *irCtrl, HeaterCoolerAccessory *heaterCooler)
    : irController(irCtrl), heaterCooler(heaterCooler) {

    active = new Characteristic::Active(0, true);  // Default to INACTIVE
    currentFanState = new Characteristic::CurrentFanState(1);  // Default to IDLE
    targetFanState = new Characteristic::TargetFanState(1, true);  // Default to AUTO
    rotationDirection = new Characteristic::RotationDirection(0, true);  // Default to CLOCKWISE
    rotationSpeed = new Characteristic::RotationSpeed(0, true);  // Default to 0%
    swingMode = new Characteristic::SwingMode(0, true);  // Default to SWING_DISABLED
    lockPhysicalControls = new Characteristic::LockPhysicalControls(0, true);  // Default to CONTROL_LOCK_DISABLED
    configuredName = new Characteristic::ConfiguredName("Fan", true);  // Default name "Fan"
}

// Update method implementation
boolean FanAccessory::update() {
    bool fanActive = active->getNewVal() == 1;
    int targetState = targetFanState->getNewVal();
    int direction = rotationDirection->getNewVal();
    float speed = rotationSpeed->getNewVal();
    bool swing = swingMode->getNewVal() == 1;
    bool lockControls = lockPhysicalControls->getNewVal() == 1;

    if (fanActive) {
        Serial.println("Fan is turned ON. Disabling HeaterCooler.");
        heaterCooler->disable();  // Disable HeaterCooler when fan is on

        // Send command to set fan mode using IR
        irController->setFanMode();

        currentFanState->setVal((targetState == 1) ? 2 : 1);  // AUTO => BLOWING, MANUAL => IDLE
    } else {
        Serial.println("Fan is turned OFF. HeaterCooler can be re-enabled.");

        irController->turnOffAC();  // Turn off the fan (AC)
        
        currentFanState->setVal(0);  // Set to INACTIVE
    }

    return true;  // Return true to indicate the update was successful
}

// Add a method to deactivate the fan when HeaterCooler is active
void FanAccessory::deactivateFan() {
    active->setVal(0);  // Set the fan to INACTIVE
    currentFanState->setVal(0);  // Set current fan state to INACTIVE
    Serial.println("Fan deactivated because HeaterCooler is enabled.");
}