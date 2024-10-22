#include "Config.h"
#include "ThermostatAccessory.h"
#include "FanAccessory.h"

extern FanAccessory* fanAccessory;

#if USE_BME680 == 1
ThermostatAccessory::ThermostatAccessory(Adafruit_BME680 *bmeSensor, IRController *irCtrl, uint8_t sdaPin, uint8_t sclPin)
    : Service::Thermostat(), bme(bmeSensor), irController(irCtrl), sdaPin(sdaPin), sclPin(sclPin) {
    Wire.setPins(sdaPin, sclPin);
    Wire.begin();
    
    if (!bme->begin()) {
        Serial.println(F("Could not find a valid BME680 sensor, check wiring!"));
        while (1);
    }

#else
ThermostatAccessory::ThermostatAccessory(DHT *dhtSensor, IRController *irCtrl)
    : Service::Thermostat(), dht(dhtSensor), irController(irCtrl) {
    dht->begin();

#endif
    irController->beginsend();
    
    currentState = new Characteristic::CurrentHeatingCoolingState(0, true);
    targetState = new Characteristic::TargetHeatingCoolingState(0, true);
    currentTemp = new Characteristic::CurrentTemperature(0);
    targetTemp = new Characteristic::TargetTemperature(22, true);
    unit = new Characteristic::TemperatureDisplayUnits(0, true);
    currentHumidity = new Characteristic::CurrentRelativeHumidity(0);


    targetTemp->setRange(16, 31, 1);  // Set range for TargetTemperature

    irController->setCharacteristics(targetState, targetTemp);
}

void ThermostatAccessory::loop() {
    unsigned long currentTime = millis();

    // Initialize lastReadTime in such a way that it forces the first read immediately
    if (lastReadTime == 0) {
        lastReadTime = currentTime - readInterval;
    }
    
    if (currentTime - lastReadTime >= readInterval) {
        lastReadTime = currentTime;
        readTemperatureAndHumidity();
    }
    
    irController->handleIR();
}

void ThermostatAccessory::readTemperatureAndHumidity() {
    // Declare variables for temperature and humidity
    float adjustedTemp = 0.0;
    float currentHumidityVal = 0.0;

#if USE_BME680 == 1
    if (!bme->performReading()) {
        Serial.println(F("Failed to read from BME680 sensor!"));
        return;
    }
    // Adjust the temperature with the offset and round to the nearest whole number
    adjustedTemp = round(bme->temperature - TEMP_OFFSET);
    currentHumidityVal = round(bme->humidity);  // Round humidity to the nearest whole number

#else
    float temperature = dht->readTemperature();
    float humidity = dht->readHumidity();
    if (!isnan(temperature) && !isnan(humidity)) {
        // Adjust the temperature with the offset and round to the nearest whole number
        adjustedTemp = round(temperature - TEMP_OFFSET);
        currentHumidityVal = round(humidity);  // Round humidity to the nearest whole number
    } else {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }
#endif

    // Only update temperature if it has changed
    if (adjustedTemp != lastSentTemp) {
        currentTemp->setVal(adjustedTemp);
        lastSentTemp = adjustedTemp;  // Store the last sent temperature
        Serial.print("Updated Temperature: ");
        Serial.println(adjustedTemp);
    }

    // Only update humidity if it has changed
    if (currentHumidityVal != lastSentHumidity) {
        currentHumidity->setVal(currentHumidityVal);
        lastSentHumidity = currentHumidityVal;  // Store the last sent humidity
        Serial.print("Updated Humidity: ");
        Serial.println(currentHumidityVal);
    }
}

boolean ThermostatAccessory::update() {
    bool power = targetState->getNewVal() != 0;
    int mode = targetState->getNewVal(); 
    int temp = targetTemp->getNewVal(); 
    int direction = fanAccessory->getrotationDirection();  
    Serial.print("Thermost switch direction: ");
    Serial.println(direction);
    fanAccessory->setrotationDirectionState(1);
    fanAccessory->CurrentFanState(0);

    irController->sendCommand(power, mode, temp);
    return true;
}

int ThermostatAccessory::getCurrentState() {
    int state = currentState->getVal();
    return state;
}

void ThermostatAccessory::setCurrentState(int state) {
    if (targetState) {
        targetState->setVal(state); 
    }
}
