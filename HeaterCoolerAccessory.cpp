#include "HeaterCoolerAccessory.h"
#include "FanAccessory.h"

extern FanAccessory* fanAccessory;
#ifdef USE_BME680
HeaterCoolerAccessory::HeaterCoolerAccessory(Adafruit_BME680 *bmeSensor, IRController *irCtrl, uint8_t sdaPin, uint8_t sclPin)
    : Service::HeaterCooler(), bme(bmeSensor), irController(irCtrl), sdaPin(sdaPin), sclPin(sclPin) {

    // Initialize I2C for BME680 with custom SDA and SCL pins
    Wire.setPins(sdaPin, sclPin);
    Wire.begin(); 

    // Attempt to initialize the BME680 sensor
    if (!bme->begin()) {
        Serial.println(F("Could not find a valid BME680 sensor, check wiring!"));
        while (1); // Loop forever if sensor initialization fails
    }

    // Configure the BME680 sensor settings
    bme->setTemperatureOversampling(BME680_OS_8X);
    bme->setHumidityOversampling(BME680_OS_2X);
    bme->setPressureOversampling(BME680_OS_4X);
    bme->setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme->setGasHeater(320, 150); // 320°C for 150 ms

#else
HeaterCoolerAccessory::HeaterCoolerAccessory(DHT *dhtSensor, IRController *irCtrl)
    : Service::HeaterCooler(), dht(dhtSensor), irController(irCtrl) {

    // Initialize the DHT sensor
    dht->begin();
#endif
    irController->beginsend();

    active = new Characteristic::Active(0, true);
    currentState = new Characteristic::CurrentHeaterCoolerState(0, true);
    targetState = new Characteristic::TargetHeaterCoolerState(0, true);
    currentTemp = new Characteristic::CurrentTemperature(0);
    coolingTemp = new Characteristic::CoolingThresholdTemperature(24, true);
    heatingTemp = new Characteristic::HeatingThresholdTemperature(27, true);
    unit = new Characteristic::TemperatureDisplayUnits(0, true);
    currentHumidity = new Characteristic::CurrentRelativeHumidity(0);
    swingMode = new Characteristic::SwingMode(0, true);

    coolingTemp->setRange(16, 31, 1);
    heatingTemp->setRange(16, 31, 1);

    SpanCharacteristic* rotationSpeed = (fanAccessory != nullptr) ? fanAccessory->getRotationSpeed() : nullptr;

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
#ifdef USE_BME680
    if (!bme->performReading()) {
        Serial.println(F("Failed to read from BME680 sensor!"));
        return;
    }
    currentTemp->setVal(bme->temperature);
    currentHumidity->setVal(bme->humidity);
#else
    float temperature = dht->readTemperature();
    float humidity = dht->readHumidity();

    if (!isnan(temperature) && !isnan(humidity)) {
        float hic = dht->computeHeatIndex(temperature, humidity, false);
        currentTemp->setVal(temperature);
        currentHumidity->setVal(humidity);

        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.print(F(" Heat index: "));
        Serial.println(hic);
    } else {
        Serial.println("Failed to read from DHT sensor!");
    }
#endif
}

boolean HeaterCoolerAccessory::update() {
    bool power = active->getNewVal() == 1;
    int mode = targetState->getNewVal();
    int temp = (mode == 1) ? heatingTemp->getNewVal() : coolingTemp->getNewVal();

    int fan = (fanAccessory != nullptr && fanAccessory->getRotationSpeed() != nullptr) ? fanAccessory->getRotationSpeed()->getNewVal() : 0;

    bool swing = swingMode->getNewVal();

    irController->sendCommand(power, mode, temp, fan, swing);
}