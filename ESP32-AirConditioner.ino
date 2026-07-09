#include <Matter.h>
#include "Config.h"
#include <WiFi.h>
#include <DHT.h>
#include "ThermostatAccessory.h"
#include "IRController.h"
#include "CustomCommissionableData.h"

ThermostatAccessory thermostatAccessory;
IRController irController(IR_SEND_PIN, IR_RECV_PIN, IR_CAPTURE_BUFFER, IR_TIMEOUT_MS, false);
DHT dht(DHTPIN, DHTTYPE);

// Read the DHT every SENSOR_READ_MS and feed temperature (with the
// self-heating offset) and humidity into the Matter endpoints.
void readSensor() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead < SENSOR_READ_MS && lastRead != 0) {
    return;
  }
  lastRead = millis();

  float temperature = dht.readTemperature();
  float humidityVal = dht.readHumidity();
  if (isnan(temperature) || isnan(humidityVal)) {
    Serial.println("[DHT] Failed to read from sensor");
    return;
  }

  float adjustedTemp = round(temperature - TEMP_OFFSET);
  Serial.printf("[DHT] Temperature: %.0f°C (raw %.1f), Humidity: %.0f%%\n",
                adjustedTemp, temperature, humidityVal);
  thermostatAccessory.updateTemperature(adjustedTemp);
  thermostatAccessory.updateHumidity(round(humidityVal));
}

// Decommission from the Matter fabric and reboot. esp_matter's factory
// reset restarts on its own; ESP.restart() is a fallback in case it returns.
void factoryReset() {
  Serial.println("Factory reset: decommissioning Matter...");
  Matter.decommission();
  delay(2000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // Connect WiFi (optional for BLE commissioning)
  // Disable modem power-save: always-powered device, and Matter latency /
  // dropped hub traffic isn't worth the few mA saved.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  #if SHOW_WIFI_STATUS
    Serial.print("Connecting to WiFi");
  #endif
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    #if SHOW_WIFI_STATUS
      Serial.print(".");
    #endif
  }
  
  #if SHOW_WIFI_STATUS
    // RSSI guide: > -60 dBm great, -70 fine, < -80 improve placement
    Serial.printf("\nWiFi connected! IP: %s, RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  #endif

  // Sync the system clock over NTP (UTC). Gives CHIP a real wall-clock time
  // for certificate validation instead of the "Last Known Good Time" fallback.
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
  {
    struct tm timeinfo;
    // SNTP keeps syncing in the background; don't block boot for more than 5s
    if (getLocalTime(&timeinfo, 5000)) {
      #if SHOW_WIFI_STATUS
        Serial.printf("Time synced: %04d-%02d-%02d %02d:%02d UTC\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min);
      #endif
    } else {
      #if SHOW_WIFI_STATUS
        Serial.println("NTP sync pending (will complete in background)");
      #endif
    }
  }

  // ✅ Initialize Thermostat endpoint BEFORE Matter.begin()
  thermostatAccessory.begin();

  // IR transceiver: Matter → IR commands, AC remote → Matter mirroring
  irController.beginSend();
  irController.beginReceive();
  thermostatAccessory.setIRController(&irController);

  dht.begin();

  #if SHOW_MATTER_STATUS
    Serial.println("[Setup] Thermostat endpoint created.");
  #endif

  // ✅ Now start the Matter node
  Matter.begin();

  // Setpoints/mode must be initialized after Matter.begin() (earlier writes
  // are clobbered by server init). Fresh device gets defaults; a
  // commissioned one keeps its persisted values, which we adopt.
  thermostatAccessory.zeroDeadband();
  if (Matter.isDeviceCommissioned()) {
    thermostatAccessory.syncFromDevice();
  } else {
    thermostatAccessory.applyDefaults();
  }

  // Use our own passcode/discriminator instead of the shared CHIP test values
  applyCustomCommissionableData();

  // Button held at power-on → factory reset (must run after Matter.begin(),
  // decommission needs the Matter stack up)
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("⚠️ Reset button held at startup!");
    factoryReset();
  }

  // ---- Stage 1: commissioning (pairing with a Matter fabric) ----
  if (!Matter.isDeviceCommissioned()) {
    #if SHOW_MATTER_STATUS
      Serial.println("\nDevice not commissioned yet.");
    #endif
    
    #if SHOW_PAIRING_CODE
      // Generated from MATTER_PASSCODE / MATTER_DISCRIMINATOR - the
      // Matter.get* variants only know the hardcoded test values.
      Serial.printf("Manual Code: %s\n", getCustomManualPairingCode().c_str());
      Serial.printf("QR URL: %s\n\n", getCustomQRCodeUrl().c_str());
    #endif

    uint32_t count = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      #if SHOW_MATTER_STATUS
        if ((count++ % 50) == 0)
          Serial.println("Waiting for commissioning...");
      #endif
    }
    #if SHOW_MATTER_STATUS
      Serial.println("Commissioned to a Matter fabric.");
    #endif
  }

  // ---- Stage 2: Matter network attach (distinct from commissioning, so a
  // paired-but-unreachable device is easy to tell apart in the logs) ----
  #if SHOW_MATTER_STATUS
    Serial.println("Waiting for Matter network connection...");
  #endif
  while (!Matter.isDeviceConnected()) {
    delay(200);
  }

  #if SHOW_MATTER_STATUS
    Serial.println("✅ Matter Thermostat commissioned, connected and ready!");
    Serial.println("Control from your Matter app to see updates.\n");
  #endif
}

void loop() {
  thermostatAccessory.tick();
  irController.handleIR();
  readSensor();

  // Long-press the reset button at runtime → factory reset
  static unsigned long pressStart = 0;
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (pressStart == 0) {
      pressStart = millis();
    } else if (millis() - pressStart > RESET_HOLD_MS) {
      factoryReset();
    }
  } else {
    pressStart = 0;
  }
  delay(50);
}
