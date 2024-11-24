#ifndef FAN_ACCESSORY_H
#define FAN_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"

class FanAccessory : public Service::Fan {
  
private:
    IRController *irController;

    SpanCharacteristic *active;
    SpanCharacteristic *rotationDirection;
    SpanCharacteristic *fanRotationSpeed;
    SpanCharacteristic *swingMode;
    SpanCharacteristic *currentFanState;

public:
    FanAccessory(IRController *irCtrl);
    boolean update();
};

#endif