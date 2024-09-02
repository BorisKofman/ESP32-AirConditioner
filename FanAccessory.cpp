#include "FanAccessory.h"
#include "HeaterCoolerAccessory.h" 

extern HeaterCoolerAccessory* heaterCoolerAccessory;

FanAccessory::FanAccessory(IRController *irCtrl) 
    : irController(irCtrl) {
    rotationDirection = new Characteristic::RotationDirection(0, true); 
    fanRotationSpeed = new Characteristic::RotationSpeed(0, true);

    fanRotationSpeed->setRange(0, 100, 25);
}

boolean FanAccessory::update() {
    int fanSpeed = fanRotationSpeed->getNewVal();  

    Serial.print("Setting fan speed to ");
    Serial.print(fanSpeed);
    Serial.println("%.");

    irController->setFanMode(fanSpeed);

    return true; 
}

// New method to get the fan speed characteristic
SpanCharacteristic* FanAccessory::getRotationSpeed() {
    return fanRotationSpeed;
}
