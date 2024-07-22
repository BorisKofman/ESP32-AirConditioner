#include "HomeSpan.h"
#include <DHT.h>
#include <BLEDevice.h>
#include "IRController.h"

#define STATUS_LED_PIN 48  // pin for status LED
#define DHT_PIN 16  // DHT11 sensor pin
#define DHT_TYPE DHT22

unsigned long lastReadTime = 0; // Variable to store the last read time
const unsigned long readInterval = 10000; // 10 seconds
const uint16_t sendPin = 4; // Define the GPIO pin for the IR LED
const uint16_t recvPin = 15; // Pin where the IR receiver is connected
const uint32_t kBaudRate = 115200;

DHT dht(DHT_PIN, DHT_TYPE);
IRController irController(sendPin, recvPin, 1024, 50, true);

class HeaterCooler : public Service::HeaterCooler {
public:
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

    HeaterCooler() : Service::HeaterCooler() {
        dht.begin();
        irController.beginsend();

        active = new Characteristic::Active(0, true);
        currentState = new Characteristic::CurrentHeaterCoolerState(0, true);
        targetState = new Characteristic::TargetHeaterCoolerState(0, true);
        currentTemp = new Characteristic::CurrentTemperature(0);
        coolingTemp = new Characteristic::CoolingThresholdTemperature(24.0, true);
        heatingTemp = new Characteristic::HeatingThresholdTemperature(27, true);
        rotationSpeed = new Characteristic::RotationSpeed(50, true);
        unit = new Characteristic::TemperatureDisplayUnits(0, true);
        currentHumidity = new Characteristic::CurrentRelativeHumidity(0);
        swingMode = new Characteristic::SwingMode(0, true);

        coolingTemp->setRange(16, 31, 1);
        heatingTemp->setRange(16, 31, 1);
        rotationSpeed->setRange(0, 100, 25);

        irController.setCharacteristics(active, currentState, coolingTemp, rotationSpeed);
    }

    void loop() {
        unsigned long currentTime = millis();
        if (currentTime - lastReadTime >= readInterval) {
            lastReadTime = currentTime;
            readTemperatureAndHumidity();
        }
        irController.handleIR();
    }

    void readTemperatureAndHumidity() {
        float temperature = dht.readTemperature();
        int humidity = dht.readHumidity();

        if (!isnan(temperature) && !isnan(humidity)) {
            int roundedTemp = round(temperature);
            currentTemp->setVal(roundedTemp);
            currentHumidity->setVal(humidity);
        } else {
            Serial.println("Failed to read from DHT sensor!");
        }
    }

    boolean update() override {
        bool power = active->getNewVal() == 1;
        int mode = targetState->getNewVal();
        int temp = (mode == 1) ? heatingTemp->getNewVal() : coolingTemp->getNewVal();
        int fan = rotationSpeed->getNewVal();
        bool swing = swingMode->getNewVal();

        irController.sendCommand(power, mode, temp, fan, swing);
        return true;
    }
};

void setup() {
    Serial.begin(kBaudRate);

    // Disable BLE to save power
    BLEDevice::deinit(true);

    irController.beginreceive();
    homeSpan.setStatusPixel(STATUS_LED_PIN, 240, 100, 5);
    homeSpan.begin(Category::AirConditioners, "Air Conditioner");
    homeSpan.enableWebLog(10, "pool.ntp.org", "UTC+3");
    homeSpan.setApTimeout(300);
    homeSpan.enableAutoStartAP();

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Name("ESP32 Air Conditioner");
    new Characteristic::Model("ESP32 AC Model");
    new Characteristic::FirmwareRevision("1.0.1");

    new HeaterCooler();
}

void loop() {
    homeSpan.poll();
}