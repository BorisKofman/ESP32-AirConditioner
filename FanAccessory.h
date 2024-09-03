#ifndef FAN_ACCESSORY_H
#define FAN_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"

class FanAccessory : public Service::Fan {
private:
    SpanCharacteristic *active;
    SpanCharacteristic *rotationDirection;
    SpanCharacteristic *fanRotationSpeed;
    SpanCharacteristic *swingMode;
    SpanCharacteristic *currentFanState;

    IRController *irController;

public:
    FanAccessory(IRController *irCtrl);
    boolean update();
    int getActiveState();  // Getter for currentState
    int getrotationDirection();  // Getter for currentState
    void CurrentFanState(int state);
    void setrotationDirectionState(int state);

};

#endif