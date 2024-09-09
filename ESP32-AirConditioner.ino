#include "HomeSpan.h"
#include "Config.h"

#if USE_BME680 == 1
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
Adafruit_BME680 bme; // I2C
#else
#include "DHT.h"
#define DHTPIN 16     // DHT sensor pin
#define DHTTYPE DHT22   // DHT sensor type
DHT dht(DHTPIN, DHTTYPE); // Initialize DHT sensor
#endif

#include "IRController.h"
#include "FanAccessory.h"
#include "VirtualSwitchAccessory.h"
#include "ThermostatAccessory.h"

#define STATUS_LED_PIN 48
#define SEND_PIN 4
#define RECV_PIN 15
#define BAUD_RATE 115200
#define CAPTURE_BUFFER_SIZE 2048
#define TIMEOUT 15

IRController irController(SEND_PIN, RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);

FanAccessory* fanAccessory = nullptr;
ThermostatAccessory* thermostatAccessory = nullptr;

void setup() {
    Serial.begin(BAUD_RATE);
    irController.beginreceive();

    homeSpan.setStatusPixel(STATUS_LED_PIN, 240, 100, 5);
    homeSpan.setStatusAutoOff(5);
    homeSpan.begin(Category::Bridges, "ACBridge");
    homeSpan.enableWebLog(10, "pool.ntp.org", "UTC+3");
    homeSpan.setApTimeout(300);
    homeSpan.enableAutoStartAP();

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();            

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify(); 
    new Characteristic::Name("Air Conditioner");
    new Characteristic::Model("ESP32 AC Model");
    new Characteristic::FirmwareRevision("1.0.3");
#if USE_BME680 == 1
    thermostatAccessory = new ThermostatAccessory(&bme, &irController, 10, 12); 
#else
    thermostatAccessory = new ThermostatAccessory(&dht, &irController);
#endif
    fanAccessory = new FanAccessory(&irController);

    // new SpanAccessory();
    // new Service::AccessoryInformation();
    // new Characteristic::Identify();
    // new Characteristic::Name("Air Conditioner Light");
    // new VirtualSwitchAccessory(&irController);  
}

void loop() {
    homeSpan.poll();
}