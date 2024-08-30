#include "FanAccessory.h"

FanAccessory::FanAccessory(IRController *irCtrl, HeaterCoolerAccessory *heaterCooler)
    : irController(irCtrl), heaterCooler(heaterCooler) {

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
    int targetState = targetFanState->getNewVal();
    int direction = rotationDirection->getNewVal();
    float speed = rotationSpeed->getNewVal();
    bool swing = swingMode->getNewVal() == 1;
    bool lockControls = lockPhysicalControls->getNewVal() == 1;

    if (fanActive) {
        Serial.println("Fan is turned ON. Disabling HeaterCooler.");
        heaterCooler->disable(); 
        heaterCooler->updateFanSpeed(speed);
        irController->setFanMode();

        currentFanState->setVal((targetState == 1) ? 2 : 1);
        

    } else {
        Serial.println("Fan is turned OFF. HeaterCooler can be re-enabled.");
        irController->turnOffAC();
        currentFanState->setVal(0); 
    }

    return true;
}

