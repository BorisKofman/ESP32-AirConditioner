#include "HomeSpan.h"
#include <DHT.h>

#define DHT_PIN 16  // DHT11 sensor pin
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

float currentTemperature = 0.0;
float currentHumidityValue = 0.0;

unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 10000; // Update every 10 seconds

int state = -1;


// Define the ACController class
struct ACController : Service::HeaterCooler {
  SpanCharacteristic *active;
  SpanCharacteristic *currentState;
  SpanCharacteristic *targetState;
  SpanCharacteristic *currentTemp;
  SpanCharacteristic *coolingTemp;
  SpanCharacteristic *heatingTemp;
  SpanCharacteristic *rotationSpeed;
  SpanCharacteristic *unit;
  SpanCharacteristic *currentHumidity;
  SpanCharacteristic *swingMode; // New characteristic for swing mode


  ACController() {
    active = new Characteristic::Active(0, true); // default to Off, stored in NVS
    currentState = new Characteristic::CurrentHeaterCoolerState(0, true); // Inactive, stored in NVS
    targetState = new Characteristic::TargetHeaterCoolerState(0, true); // Auto, stored in NVS
    currentTemp = new Characteristic::CurrentTemperature(currentTemperature);
    coolingTemp = new Characteristic::CoolingThresholdTemperature(24.0, true); // default cooling temp, stored in NVS
    heatingTemp = new Characteristic::HeatingThresholdTemperature(27, true); // default heating temp, stored in NVS
    rotationSpeed = new Characteristic::RotationSpeed(50, true); // default fan speed, stored in NVS
    unit = new Characteristic::TemperatureDisplayUnits(0, true); // 0 for Celsius, stored in NVS
    currentHumidity = new Characteristic::CurrentRelativeHumidity(currentHumidityValue);
    swingMode = new Characteristic::SwingMode(0, true); // default to Swing Disabled, stored in NVS

    coolingTemp->setRange(16, 28, 0.5); // Set valid range for cooling temperature
    heatingTemp->setRange(20, 32, 0.5); // Set valid range for heating temperature
    rotationSpeed->setRange(0, 100, 25); // Set valid range for rotation speed (0, 25, 50, ..., 100)


  }

  boolean update() override {
    Serial.println("Update called"); 
    // Check and print only the modified changes
    if (active->getNewVal() == 0) {
      // Turn Off the AC
      Serial.print("Turn off AC: "); 
      return true;
    }

    if (active->getNewVal() == 1 || targetState->getNewVal())  {
      // Turn On the AC
      Serial.print("Turn on AC: "); 
      if (targetState->getNewVal() == 0 ) { // Check if auto state 
        state = 0;
      }
      if (targetState->getNewVal() == 1 ) { // Check if Heating state
        state = 1;
      }
      if (targetState->getNewVal() == 2 ) { // Check if Cooling state 
        state = 2;
      }
    }

    if (state == 0 && heatingTemp->getNewVal()) {
    Serial.print("state: Auto "); Serial.println(heatingTemp->getNewVal());
    return true;
    }

    if (state == 1 && heatingTemp->getNewVal()) {
    Serial.print("state: Heating "); Serial.println(heatingTemp->getNewVal());
    return true;
    }

    if (state == 2 && coolingTemp->getNewVal()) {
    Serial.print("state: Cooling "); Serial.println(coolingTemp->getNewVal());
    return true;
    }
  }

  void readTemperatureAndHumidity() {
    float temperature = dht.readTemperature(unit->getNewVal() == 1); // true for Fahrenheit
    float humidity = dht.readHumidity();
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }
    
    // Update HomeKit characteristics
    currentTemp->setVal(temperature);
    currentHumidity->setVal(humidity);
  }
};

ACController *acController;  // Global variable to access the ACController instance

void setup() {
  Serial.begin(115200);
  dht.begin();
  //wifi
  homeSpan.setWifiCredentials("YOUR-SSID", "Your-password");
  homeSpan.begin(Category::AirConditioners, "Air Conditioner");

  SpanAccessory *accessory = new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Name("ESP32 Air Conditioner");
  new Characteristic::Manufacturer("ESP32");
  new Characteristic::SerialNumber("123-ABC");
  new Characteristic::Model("ESP32 AC Model");
  new Characteristic::FirmwareRevision("1.1");
  new Characteristic::Identify();

  acController = new ACController();  // Instantiate the ACController
}

void loop() {
  homeSpan.poll();
  
  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdateTime >= updateInterval) {
    lastUpdateTime = currentMillis;
    acController->readTemperatureAndHumidity();
  }
}