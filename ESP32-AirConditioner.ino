#include "HomeSpan.h"
#include <DHT.h>

//For IR 
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

#include <Preferences.h>  // For NVS

#include <ir_Goodweather.h>

#define DHT_PIN 21  // DHT11 sensor pin
//define DHT_TYPE DHT22
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastReadTime = 0; // Variable to store the last read time
const unsigned long readInterval = 10000; // 10 seconds

const uint16_t kIrLedPin = 4; // Define the GPIO pin for the IR LED
const uint16_t kRecvPin = 14; // Pin where the IR receiver is connected

const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50;  // Timeout for A/C messages
const uint8_t kTolerancePercentage = kTolerance;

//IR receiver
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;
//IR transmiter 
IRsend irsend(kIrLedPin);

// Initialize the object with send pin only
IRGoodweatherAc ac(kIrLedPin);


Preferences preferences;


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
    irsend.begin();

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
  }

  void loop() {
    unsigned long currentTime = millis();
    if (currentTime - lastReadTime >= readInterval) {
      lastReadTime = currentTime;
      readTemperatureAndHumidity();
    }
    if (irrecv.decode(&results)) {
      ac.setRaw(results.value);  // Set the internal state from the received IR signal
      active->setVal(ac.getPower());
      currentState->setVal(ac.getMode());
      coolingTemp->setVal(ac.getTemp());
      
    }
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
    if (active->getNewVal() == 0) {
      // Turn Off the AC
      ac.setPower(false); // Send IR command to turn off the AC
      irsend.sendGoodweather(ac.getRaw(), kGoodweatherBits);
      return true;
    } else if (active->getNewVal() == 1) {
      // Turn On the AC
      ac.setPower(true); // Ensure the AC is on

      // Set fan speed based on rotationSpeed
      int speed = rotationSpeed->getNewVal();
      ac.setFan((speed <= 25) ? kGoodweatherFanLow :
          (speed <= 50) ? kGoodweatherFanMed :
          (speed <= 75) ? kGoodweatherFanHigh :
                          kGoodweatherFanAuto);
                          
      ac.setSwing(swingMode->getNewVal());  // Set swing mode
      int state = targetState->getNewVal();
      currentState->setVal(state == 0 ? 1 : (state == 1 ? 2 : 3)); // Set current state based on target state
      switch (state) {
      case 0:
          ac.setMode(kGoodweatherAuto);
          ac.setTemp(coolingTemp->getNewVal());  // Set temperature
          break;
      case 1:
          ac.setMode(kGoodweatherHeat);
          ac.setTemp(heatingTemp->getNewVal());  // Set temperature
          break;
      case 2:
          ac.setMode(kGoodweatherCool);
          ac.setTemp(coolingTemp->getNewVal());  // Set temperature
          break;
      }
 
    // Send IR command for cooling mode with specified settings
    irsend.sendGoodweather(ac.getRaw(), kGoodweatherBits);
    return true;
    }
  } 
};

void setup() {
  Serial.begin(kBaudRate);

  irrecv.setTolerance(kTolerancePercentage);
  irrecv.enableIRIn();

  homeSpan.begin(Category::AirConditioners, "Air Conditioner");
  homeSpan.setApTimeout(180); // Set the timeout to 180 seconds (adjust as needed)
  homeSpan.enableAutoStartAP();
  // homeSpan.setControlCallback(pairUnpairCallback); // Set the pairing/unpairing callback function in the future 

new SpanAccessory();
  new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Name("ESP32 Air Conditioner");
    new Characteristic::Model("ESP32 AC Model");
    new Characteristic::FirmwareRevision("1.0.1");




  new HeaterCooler();
}

void loop() {
  // Check if the IR code has been received.
if (irrecv.decode(&results)) {
    preferences.begin("ac_ctrl", false);  // Re-open NVS storage with namespace "esp32_air_conditioner"
    String irType = typeToString(results.decode_type);
    if (!preferences.isKey("irType") && preferences.putString("irType", irType)) {
        Serial.println(irType);
    }
    preferences.end();  // Close NVS storage
}
  homeSpan.poll();
}

