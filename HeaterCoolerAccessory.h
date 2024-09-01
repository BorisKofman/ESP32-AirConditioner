#ifndef HEATERCOOLER_ACCESSORY_H
#define HEATERCOOLER_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"
#include <DHT.h>

class HeaterCoolerAccessory : public Service::HeaterCooler {
private:
    DHT *dht;
    IRController *irController;
    unsigned long lastReadTime = 0;
    const unsigned long readInterval = 2000;

    SpanCharacteristic *active;
    SpanCharacteristic *currentState;
    SpanCharacteristic *targetState;
    SpanCharacteristic *currentTemp;
    SpanCharacteristic *coolingTemp;
    SpanCharacteristic *heatingTemp;
    SpanCharacteristic *rotationSpeed;
    SpanCharacteristic *unit;
    SpanCharacteristic *currentHumidity;
    SpanCharacteristic *swingMode;

public:
    HeaterCoolerAccessory(DHT *dhtSensor, IRController *irCtrl);
    boolean update() override;
    void disable();
    void enable();
    int getActiveState();  
    
    void loop(); 
    void readTemperatureAndHumidity();
};

#endif