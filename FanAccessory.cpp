#include "FanAccessory.h"
#include "ThermostatAccessory.h" 

extern ThermostatAccessory* thermostatAccessory;

FanAccessory::FanAccessory(IRController *irCtrl) 
    : irController(irCtrl) {
    
    active = new Characteristic::Active(0, true);
    rotationDirection = new Characteristic::RotationDirection(0, true); 
    fanRotationSpeed = new Characteristic::RotationSpeed(25, true);
    swingMode = new Characteristic::SwingMode(0, true);
    currentFanState = new Characteristic::CurrentFanState(0, true);

    fanRotationSpeed->setRange(0, 100, 25);
}

boolean FanAccessory::update() {
    int fanSpeed = fanRotationSpeed->getNewVal();  
    bool swing = swingMode->getNewVal();
    int direction = rotationDirection->getNewVal(); 
    int thermostanstate = thermostatAccessory->getCurrentState(); 
    int power = 0;
    Serial.print("fan switch direction: ");
    Serial.println(direction);
    if (direction == 0) { 
        thermostatAccessory->setCurrentState(0);
        Serial.println(fanSpeed);
        if (fanSpeed > 1) {
            power = 1;
        } else {
            power = 0;
        }
    } else { 
      active->setVal(0); 
    }

    irController->setFanMode(power, fanSpeed, swing, direction);
    return true; 
}


int FanAccessory::getActiveState() {
    int state = active->getVal();
    return state;
}

int FanAccessory::getrotationDirection() {
    int direction = rotationDirection->getVal();
    return direction;
}

void FanAccessory::CurrentFanState(int state) {
      active->setVal(state);
}

void FanAccessory::setrotationDirectionState(int state) {
    if (rotationDirection) {
        rotationDirection->setVal(state);
    }
}


