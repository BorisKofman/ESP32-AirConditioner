#ifndef HEATERCOOLER_ACCESSORY_H
#define HEATERCOOLER_ACCESSORY_H

#include "HomeSpan.h"
#include "IRController.h"

#include <DHT.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"


// Include guard for USE_BME680 to ensure consistent conditional compilation
#ifdef USE_BME680
#define USE_BME680 1
#else
#define USE_BME680 0
#endif

class HeaterCoolerAccessory : public Service::HeaterCooler {
private:
#ifdef USE_BME680
    Adafruit_BME680 *bme;
    uint8_t sdaPin;
    uint8_t sclPin;
#else
    DHT *dht;
#endif
    IRController *irController;
    unsigned long lastReadTime = 0;
    const unsigned long readInterval = 2000;

    SpanCharacteristic *active;
    SpanCharacteristic *currentState;
    SpanCharacteristic *targetState;
    SpanCharacteristic *currentTemp;
    SpanCharacteristic *coolingTemp;
    SpanCharacteristic *heatingTemp;
    SpanCharacteristic *unit;
    SpanCharacteristic *currentHumidity;
    SpanCharacteristic *swingMode;

public:
#ifdef USE_BME680
    HeaterCoolerAccessory(Adafruit_BME680 *bmeSensor, IRController *irCtrl, uint8_t sdaPin, uint8_t sclPin);
#else
    HeaterCoolerAccessory(DHT *dhtSensor, IRController *irCtrl);
#endif
    boolean update() override;    
    void loop(); 
    void readTemperatureAndHumidity();
};

#endif