#ifndef FAN_ACCESSORY_H
#define FAN_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"
#include "HeaterCoolerAccessory.h"

class FanAccessory : public Service::Fan {
private:
    SpanCharacteristic *active;
    SpanCharacteristic *currentFanState;
    SpanCharacteristic *targetFanState;
    SpanCharacteristic *rotationDirection;
    SpanCharacteristic *rotationSpeed;
    SpanCharacteristic *swingMode;
    SpanCharacteristic *lockPhysicalControls;
    SpanCharacteristic *configuredName;
    
    IRController *irController;
    HeaterCoolerAccessory *heaterCooler;

public:
    FanAccessory(IRController *irCtrl, HeaterCoolerAccessory *heaterCooler);
    boolean update() override;
};

#endif