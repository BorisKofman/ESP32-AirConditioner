#ifndef FAN_ACCESSORY_H
#define FAN_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"

#include <Ticker.h> // Include the Ticker library

class FanAccessory : public Service::Fan {
  
private:
    IRController *irController;

    SpanCharacteristic *active;
    SpanCharacteristic *rotationDirection;
    SpanCharacteristic *fanRotationSpeed;
    SpanCharacteristic *swingMode;
    SpanCharacteristic *currentFanState;

    void setInactive();
    Ticker inactiveTimer;

public:
    FanAccessory(IRController *irCtrl);
    boolean update();
};

#endif