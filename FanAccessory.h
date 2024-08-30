#ifndef FAN_ACCESSORY_H
#define FAN_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"
#include "HeaterCoolerAccessory.h"

class FanAccessory : public Service::Fan {
private:
    SpanCharacteristic *on;          // Characteristic to represent the fan state
    IRController *irController;      // Pointer to the IRController instance
    HeaterCoolerAccessory *heaterCooler;  // Pointer to the HeaterCoolerAccessory to disable

public:
    FanAccessory(IRController *irCtrl, HeaterCoolerAccessory *heaterCooler);

    boolean update() override;
};

#endif // FAN_ACCESSORY_H