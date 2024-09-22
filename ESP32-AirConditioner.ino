#include "HomeSpan.h"
#include "Config.h"
#ifndef BEACON
#include <esp_bt.h>
#include <esp_bt_main.h>
#endif
#include <HardwareSerial.h>

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
#include "RadarAccessory.h" 

#if defined(USE_LD2412)
#include "LD2412.h"  
typedef LD2412 RadarType;
const int baudRate = 115200;  // Default baud rate for LD2412
HardwareSerial radarSerial(1); 
RadarType radar(radarSerial);  // Pass radarSerial to the LD2412 constructor
const int dataBits = SERIAL_8N1;

#elif defined(USE_LD2410)
#include <ld2410.h>  
typedef ld2410 RadarType;
const int baudRate = 256000;
HardwareSerial radarSerial(1); 
RadarType radar;  // Radar for LD2410 (no constructor call)
const int dataBits = SERIAL_8N1;

#endif

IRController irController(SEND_PIN, RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);

FanAccessory* fanAccessory = nullptr;
ThermostatAccessory* thermostatAccessory = nullptr;

#ifdef BEACON
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEBeacon.h>

void initBLEBeacon() {
    BLEDevice::init("");  // No device name

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    BLEAdvertisementData advertisementData;
    BLEBeacon oBeacon = BLEBeacon();

    oBeacon.setManufacturerId(0x004C);  // Apple's manufacturer ID
    oBeacon.setProximityUUID(BLEUUID(BEACON_UUID));
    oBeacon.setMajor(major);
    oBeacon.setMinor(minor);
    oBeacon.setSignalPower(txPower);

    advertisementData.setFlags(ESP_BLE_ADV_FLAG_NON_LIMIT_DISC);

    String strServiceData = "";
    strServiceData += (char)26;        // Length of service data
    strServiceData += (char)0xFF;      // Manufacturer specific data type
    strServiceData += oBeacon.getData();

    advertisementData.addData(strServiceData);

    pAdvertising->setAdvertisementData(advertisementData);

    // Create an empty BLEAdvertisementData object for scan response
    BLEAdvertisementData scanResponseData;
    // Optionally, you can set scan response data here if needed
    pAdvertising->setScanResponseData(scanResponseData);

    pAdvertising->start();

    Serial.println("iBeacon started");
}
#endif

void setup() {
    Serial.begin(BAUD_RATE);

    irController.beginreceive();

    homeSpan.setStatusPixel(STATUS_LED_PIN, 240, 100, 5);
    homeSpan.setStatusAutoOff(5);
    homeSpan.begin(Category::Bridges, "ACBridge");
    homeSpan.enableWebLog(10, "pool.ntp.org", "UTC+3");
    homeSpan.setApTimeout(300);
    homeSpan.enableAutoStartAP();

#if defined(USE_LD2412) || defined(USE_LD2410)
    radarSerial.begin(baudRate, dataBits, rxPin, txPin);
    delay(500);
    radar.begin(radarSerial);
    #if defined(USE_LD2412)
        Serial.println("LD2412 radar sensor initialized successfully.");
    #elif defined(USE_LD2410)
        Serial.println("LD2410 radar sensor initialized successfully.");
    #endif
#endif

#ifdef BEACON
    // Initialize iBeacon
    initBLEBeacon();
#else
    // Disable Bluetooth to save power
    btStop();
    esp_bt_controller_disable();
#endif

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();            

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify(); 
    new Characteristic::Name("Air Conditioner");
    new Characteristic::Model("ESP32 AC Model");
    new Characteristic::FirmwareRevision("1.2.1");
#if USE_BME680 == 1
    thermostatAccessory = new ThermostatAccessory(&bme, &irController, 10, 12); 
#else
    thermostatAccessory = new ThermostatAccessory(&dht, &irController);
#endif
    fanAccessory = new FanAccessory(&irController);

#if defined(USE_LD2412) || defined(USE_LD2410)
    new SpanAccessory();                          
    new Service::AccessoryInformation();
    new Characteristic::Identify(); 
    new Characteristic::Name("Radar Sensor 1");
    new RadarAccessory(&radar, 0, 1100);  
#endif

    // new SpanAccessory();
    // new Service::AccessoryInformation();
    // new Characteristic::Identify();
    // new Characteristic::Name("Air Conditioner Light");
    // new VirtualSwitchAccessory(&irController);  
}

void loop() {
    homeSpan.poll();
#if defined(USE_LD2412) || defined(USE_LD2410)
    radar.read(); 
#endif
}