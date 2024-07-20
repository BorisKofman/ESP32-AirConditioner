#include "HomeSpan.h"
#include <DHT.h>
#include <BLEDevice.h>
#include "IRController.h"

#define STATUS_LED_PIN 48  // pin for status LED
#define DHT_PIN 5  // DHT11 sensor pin
#define DHT_TYPE DHT22

unsigned long lastReadTime = 0; // Variable to store the last read time
const unsigned long readInterval = 10000; // 10 seconds
const uint16_t kIrLedPin = 16; // Define the GPIO pin for the IR LED
const uint16_t kRecvPin = 15; // Pin where the IR receiver is connected
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50;  // Timeout for A/C messages

DHT dht(DHT_PIN, DHT_TYPE);
IRController irController(kIrLedPin, kRecvPin, kCaptureBufferSize, kTimeout, true);

class HeaterCooler : public Service::HeaterCooler {
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
    HeaterCooler() : Service::HeaterCooler() {
        dht.begin();
        irController.begin();

        active = new Characteristic::Active(0, true); // default to Off, stored in NVS
        currentState = new Characteristic::CurrentHeaterCoolerState(0, true); // Inactive, stored in NVS
        targetState = new Characteristic::TargetHeaterCoolerState(0, true); // Auto, stored in NVS
        currentTemp = new Characteristic::CurrentTemperature(0);
        coolingTemp = new Characteristic::CoolingThresholdTemperature(24.0, true); // default cooling temp, stored in NVS
        heatingTemp = new Characteristic::HeatingThresholdTemperature(27, true); // default heating temp, stored in NVS
        rotationSpeed = new Characteristic::RotationSpeed(50, true); // default fan speed, stored in NVS
        unit = new Characteristic::TemperatureDisplayUnits(0, true); // 0 for Celsius, stored in NVS
        currentHumidity = new Characteristic::CurrentRelativeHumidity(0);
        swingMode = new Characteristic::SwingMode(0, true); // default to Swing Disabled, stored in NVS

        coolingTemp->setRange(16, 31, 1); // Set valid range for cooling temperature
        heatingTemp->setRange(16, 31, 1); // Set valid range for heating temperature
        rotationSpeed->setRange(0, 100, 25); // Set valid range for rotation speed (0, 25, 50, ..., 100)

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

    // homeSpan.setControlPin(DEVICE_CONTROL_PIN).setStatusPin(STATUS_LED_PIN);
    homeSpan.setStatusPixel(STATUS_LED_PIN);
    homeSpan.begin(Category::AirConditioners, "Air Conditioner");
    homeSpan.enableWebLog(10, "pool.ntp.org", "UTC+3");

    homeSpan.setApTimeout(300); // Set the timeout to 180 seconds (adjust as needed)
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