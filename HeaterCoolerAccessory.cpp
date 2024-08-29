#include "HeaterCoolerAccessory.h"

HeaterCoolerAccessory::HeaterCoolerAccessory(DHT *dhtSensor, IRController *irCtrl) 
    : dht(dhtSensor), irController(irCtrl) {

    dht->begin();
    irController->beginsend();

    active = new Characteristic::Active(0, true);
    currentState = new Characteristic::CurrentHeaterCoolerState(0, true);
    targetState = new Characteristic::TargetHeaterCoolerState(0, true);
    currentTemp = new Characteristic::CurrentTemperature(0);
    coolingTemp = new Characteristic::CoolingThresholdTemperature(24, true);
    heatingTemp = new Characteristic::HeatingThresholdTemperature(27, true);
    rotationSpeed = new Characteristic::RotationSpeed(50, true);
    unit = new Characteristic::TemperatureDisplayUnits(0, true);
    currentHumidity = new Characteristic::CurrentRelativeHumidity(0);
    swingMode = new Characteristic::SwingMode(0, true);

    coolingTemp->setRange(16, 31, 1);
    heatingTemp->setRange(16, 31, 1);
    rotationSpeed->setRange(0, 100, 25);

    irController->setCharacteristics(active, currentState, coolingTemp, rotationSpeed);
}

void HeaterCoolerAccessory::loop() {
    unsigned long currentTime = millis();
    if (currentTime - lastReadTime >= readInterval) {
        lastReadTime = currentTime;
        readTemperatureAndHumidity();
    }
    irController->handleIR();
}

void HeaterCoolerAccessory::readTemperatureAndHumidity() {
    float temperature = dht->readTemperature();
    int humidity = dht->readHumidity();

    if (!isnan(temperature) && !isnan(humidity)) {
        int roundedTemp = round(temperature);
        currentTemp->setVal(roundedTemp);
        currentHumidity->setVal(humidity);
    } else {
        Serial.println("Failed to read from DHT sensor!");
    }
}

boolean HeaterCoolerAccessory::update() {
    bool power = active->getNewVal() == 1;
    int mode = targetState->getNewVal();
    int temp = (mode == 1) ? heatingTemp->getNewVal() : coolingTemp->getNewVal();
    int fan = rotationSpeed->getNewVal();
    bool swing = swingMode->getNewVal();

    irController->sendCommand(power, mode, temp, fan, swing);
    return true;
}