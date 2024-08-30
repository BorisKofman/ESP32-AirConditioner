#ifndef HEATERCOOLER_ACCESSORY_H
#define HEATERCOOLER_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"
#include "FanAccessory.h"
#include <DHT.h>

class HeaterCoolerAccessory : public Service::HeaterCooler {
private:
    DHT *dht;
    IRController *irController;

    // Add FanAccessory pointer to control fan
    FanAccessory *fanAccessory;

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

    unsigned long lastReadTime = 0;
    const unsigned long readInterval = 10000;

public:
    HeaterCoolerAccessory(DHT *dhtSensor, IRController *irCtrl, FanAccessory *fanAcc);
    void loop();
    void readTemperatureAndHumidity();
    boolean update() override;
    void disable();
    void updateFanSpeed(float speed);
};

#endif