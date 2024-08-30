#include "FanAccessory.h"

// Constructor implementation
FanAccessory::FanAccessory(IRController *irCtrl, HeaterCoolerAccessory *heaterCooler)
    : irController(irCtrl), heaterCooler(heaterCooler) {

    on = new Characteristic::On(0, true);  // Initialize the fan with off state (0)
}

// Update method implementation
boolean FanAccessory::update() {
    bool state = on->getNewVal();  // Get the new state of the fan switch

    if (state) {
        Serial.println("Fan is turned ON. Disabling HeaterCooler.");
        heaterCooler->disable();  // Disable HeaterCooler when fan is on
        irController->setFanMode();  // Set AC to fan mode
    } else {
        Serial.println("Fan is turned OFF. Sending shutdown command to AC.");
        // Send shutdown command to AC
        irController->turnOffAC();  // Use the new method to turn off the AC
    }

    return true;  // Return true to indicate the update was successful
}