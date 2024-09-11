#include "HomeSpan.h"
#include "Config.h"
#include <esp_bt.h>
#include <esp_bt_main.h>

#if USE_BME680 == 1
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
Adafruit_BME680 bme; // I2C
#else
#include "DHT.h"
DHT dht(DHTPIN, DHTTYPE); // Initialize DHT sensor
#endif

#include "IRController.h"
#include "FanAccessory.h"
#include "VirtualSwitchAccessory.h"
#include "ThermostatAccessory.h"

IRController irController(SEND_PIN, RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);

FanAccessory* fanAccessory = nullptr;
ThermostatAccessory* thermostatAccessory = nullptr;

void setup() {
    Serial.begin(BAUD_RATE);

    // Disable Bluetooth to save power
    btStop();
    esp_bt_controller_disable();

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
    new Characteristic::FirmwareRevision("1.1.2");
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