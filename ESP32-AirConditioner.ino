#include <Matter.h>
#include "Config.h"
#if !USE_THREAD
  #include <WiFi.h>
#endif
#include <DHT.h>
#include "ThermostatAccessory.h"
#include "IRController.h"
#include "CustomCommissionableData.h"

ThermostatAccessory thermostatAccessory;
IRController irController(IR_SEND_PIN, IR_RECV_PIN, IR_CAPTURE_BUFFER, IR_TIMEOUT_MS, false);
DHT dht(DHTPIN, DHTTYPE);

// Feed DHT temperature (offset-corrected) and humidity into Matter
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

// Decommission from the Matter fabric and reboot
void factoryReset() {
  Serial.println("Factory reset: decommissioning Matter...");
  Matter.decommission();
  delay(2000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

#if USE_THREAD
  // Matter over Thread: Matter.begin() brings up the 802.15.4 stack and the
  // device joins the Thread mesh via BLE commissioning (credentials pushed by
  // the commissioner). No WiFi association here; a Thread Border Router on the
  // network bridges the mesh to IP. NTP is reachable through the border router
  // but not needed for this device, so it is skipped on Thread.
  #if SHOW_WIFI_STATUS
    Serial.println("Transport: Matter over Thread (no WiFi association)");
  #endif
#else
  // No modem power-save (always-powered; avoids missed hub ACKs)
  WiFi.setSleep(false);
  // Scan all channels, join the strongest AP (mesh: nearest node, not first)
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  #if SHOW_WIFI_STATUS
    Serial.print("Connecting to WiFi");
  #endif

  {
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      attempts++;
      #if SHOW_WIFI_STATUS
        Serial.print(".");
        // Status: 1=SSID not found, 4=connect failed, 6=disconnected
        if (attempts % 20 == 0) {
          Serial.printf("\nWiFi status: %d (attempt %d)\n", WiFi.status(), attempts);
        }
      #endif
      // Kick a wedged connection attempt
      if (attempts % 40 == 0) {
        Serial.println("\nWiFi retry: restarting connection attempt");
        WiFi.disconnect();
        delay(100);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
    }
  }

  #if SHOW_WIFI_STATUS
    // RSSI: > -60 great, -70 fine, < -80 improve placement
    Serial.printf("\nWiFi connected! IP: %s, RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  #endif

  // Background NTP - never blocks boot
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
#endif  // USE_THREAD

  // Endpoints must exist before Matter.begin()
  thermostatAccessory.begin();

  irController.beginSend();
  irController.beginReceive();
  thermostatAccessory.setIRController(&irController);

  dht.begin();

  #if SHOW_MATTER_STATUS
    Serial.println("[Setup] Thermostat endpoint created.");
  #endif

  Matter.begin();

  // Value writes only work after Matter.begin() (server init clobbers earlier ones)
  thermostatAccessory.zeroDeadband();
  if (Matter.isDeviceCommissioned()) {
    thermostatAccessory.syncFromDevice();
  } else {
    thermostatAccessory.applyDefaults();
  }

  // Custom passcode/discriminator instead of the shared CHIP test values
  applyCustomCommissionableData();

  // Button held at power-on → factory reset (needs the Matter stack up)
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("⚠️ Reset button held at startup!");
    factoryReset();
  }

  // Stage 1: commissioning
  if (!Matter.isDeviceCommissioned()) {
    #if SHOW_MATTER_STATUS
      Serial.println("\nDevice not commissioned yet.");
    #endif

    #if SHOW_PAIRING_CODE
      // Generated from MATTER_PASSCODE / MATTER_DISCRIMINATOR
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

  // Stage 2: network attach (distinct log so pairing vs network issues differ)
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

  // Long-press → factory reset
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
