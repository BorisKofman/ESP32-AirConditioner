#include "HomeSpan.h"
#include "IRController.h"
#include "HeaterCoolerAccessory.h"
#include "VirtualSwitchAccessory.h"
#include "FanAccessory.h"

#define STATUS_LED_PIN 48
#define DHT_PIN 16
#define DHT_TYPE DHT22
#define SEND_PIN 4
#define RECV_PIN 15
#define BAUD_RATE 115200
#define CAPTURE_BUFFER_SIZE 2048
#define TIMEOUT 15

DHT dht(DHT_PIN, DHT_TYPE);
IRController irController(SEND_PIN, RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);

HeaterCoolerAccessory* heaterCooler;  

void setup() {
    Serial.begin(BAUD_RATE);

    irController.beginreceive(); 

    homeSpan.setStatusPixel(STATUS_LED_PIN, 240, 100, 5);
    homeSpan.begin(Category::Bridges, "ESP32 Air Conditioner Bridge");
    homeSpan.enableWebLog(10, "pool.ntp.org", "UTC+3");
    homeSpan.setApTimeout(300);
    homeSpan.enableAutoStartAP();

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();            
 
    new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify(); 
      new Characteristic::Name("ESP32 Air Conditioner");
      new Characteristic::Model("ESP32 AC Model");
      new Characteristic::FirmwareRevision("1.0.1");
      heaterCooler = new HeaterCoolerAccessory(&dht, &irController); 

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Name("ESP32 Air Conditioner Fan");
    new FanAccessory(&irController, heaterCooler);

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Name("Air Conditioner Light");
    new VirtualSwitchAccessory(&irController);  
}

void loop() {
    homeSpan.poll();
}