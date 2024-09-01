#include "FanAccessory.h"
#include "HeaterCoolerAccessory.h"  // To access HeaterCoolerAccessory

extern HeaterCoolerAccessory* heaterCoolerAccessory;  // Assume global pointer defined

FanAccessory::FanAccessory(IRController *irCtrl) 
    : irController(irCtrl) {
    active = new Characteristic::Active(0, true);  
    currentFanState = new Characteristic::CurrentFanState(1);
    targetFanState = new Characteristic::TargetFanState(1, true); 
    rotationDirection = new Characteristic::RotationDirection(0, true); 
    rotationSpeed = new Characteristic::RotationSpeed(0, true);  
    swingMode = new Characteristic::SwingMode(0, true);  
    lockPhysicalControls = new Characteristic::LockPhysicalControls(0, true);
    configuredName = new Characteristic::ConfiguredName("Fan", true);

    
}

boolean FanAccessory::update() {
    bool fanActive = active->getNewVal() == 1;

    if (fanActive) {
        Serial.println("Fan is turned ON.");
        irController->setFanMode();
        currentFanState->setVal((targetFanState->getNewVal() == 1) ? 2 : 1);

        // Use the new getter method to check if the heater is active
        if (heaterCoolerAccessory != nullptr && heaterCoolerAccessory->getActiveState() == 1) {
            heaterCoolerAccessory->disable();
            Serial.println("FanAccessory has disabled HeaterCoolerAccessory.");
        }

    } else {
        Serial.println("Fan is turned OFF.");
        irController->turnOffAC();
        currentFanState->setVal(0); 
    }
    
    return true;
}

void FanAccessory::disable() {
    if (active != nullptr && active->getVal() != 0) {
        active->setVal(0, true);
        Serial.println("FanAccessory disabled.");
    }
}