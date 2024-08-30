#include "VirtualSwitchAccessory.h"

VirtualSwitchAccessory::VirtualSwitchAccessory(IRController *irCtrl) 
    : irController(irCtrl) { 
      
    on = new Characteristic::On(0, true);
}

boolean VirtualSwitchAccessory::update() {
    bool state = on->getNewVal(); 

    irController->setLight(state);

    return true;  
}