#include "FanAccessory.h"
#include "ThermostatAccessory.h" 

extern ThermostatAccessory* thermostatAccessory;

FanAccessory::FanAccessory(IRController *irCtrl) 
    : irController(irCtrl) {
    
    active = new Characteristic::Active(0, true);
    // rotationDirection = new Characteristic::RotationDirection(0, true); 
    fanRotationSpeed = new Characteristic::RotationSpeed(25, true);
    swingMode = new Characteristic::SwingMode(0, true);
    currentFanState = new Characteristic::CurrentFanState(0, true);

    fanRotationSpeed->setRange(0, 100, 25);
}

boolean FanAccessory::update() {
    int fanSpeed = fanRotationSpeed->getNewVal();  
    bool swing = swingMode->getNewVal();
    // int direction = rotationDirection->getNewVal(); 
    active->setVal(0); 
    irController->sendFanCommand(fanSpeed, swing);
    return true; 
}



