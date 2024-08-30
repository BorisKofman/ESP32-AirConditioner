#ifndef VIRTUAL_SWITCH_ACCESSORY_H
#define VIRTUAL_SWITCH_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"

class VirtualSwitchAccessory : public Service::Switch {
private:
    SpanCharacteristic *on;
    IRController *irController;

public:
    VirtualSwitchAccessory(IRController *irCtrl);

    boolean update() override;
};

#endif


