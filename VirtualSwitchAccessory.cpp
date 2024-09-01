#include "VirtualSwitchAccessory.h"

VirtualSwitchAccessory::VirtualSwitchAccessory(IRController *irCtrl) 
    : irController(irCtrl) { 
    on = new Characteristic::On(0, true);
}