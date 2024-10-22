#ifndef THERMOSTAT_ACCESSORY_H
#define THERMOSTAT_ACCESSORY_H

#include "HomeSpan.h"
#include "Config.h"  
#include "IRController.h"

#if USE_BME680
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#else
#include <DHT.h>
#endif

class ThermostatAccessory : public Service::Thermostat {
private:
#if USE_BME680 == 1
    Adafruit_BME680 *bme;
    uint8_t sdaPin;
    uint8_t sclPin;
#else
    DHT *dht;
#endif
    IRController *irController;
    unsigned long lastReadTime = 0;
    const unsigned long readInterval = 60000;
    float lastSentTemp = 0.0;
    float lastSentHumidity = 0.0;

    SpanCharacteristic *currentState;
    SpanCharacteristic *targetState;
    SpanCharacteristic *currentTemp;
    SpanCharacteristic *unit;
    SpanCharacteristic *targetTemp; 

    SpanCharacteristic *currentHumidity;

public:
#if USE_BME680  
    ThermostatAccessory(Adafruit_BME680 *bmeSensor, IRController *irCtrl, uint8_t sdaPin, uint8_t sclPin);
#else
    ThermostatAccessory(DHT *dhtSensor, IRController *irCtrl);
#endif

    boolean update();
    void loop(); 
    void readTemperatureAndHumidity();
    void setCurrentState(int state);
    int getCurrentState();

};

#endif 
