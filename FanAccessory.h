#ifndef FAN_ACCESSORY_H
#define FAN_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"

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

public:
    FanAccessory(IRController *irCtrl);
    boolean update() override;
    void disable();
};

#endif