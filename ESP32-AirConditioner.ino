
#include <Matter.h>
#include "Config.h"

#include <WiFi.h>
#include "ThermostatAccessory.h"
#include <math.h>

ThermostatAccessory thermostatAccessory;

uint32_t button_time_stamp = 0;
bool button_state = false;
const uint32_t DECOMMISSION_TIMEOUT_MS = 5000;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  Matter.begin();

  // Wait for commissioning
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("\nDevice not commissioned yet.");
    Serial.printf("Manual Code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR URL: %s\n\n", Matter.getOnboardingQRCodeUrl().c_str());

    uint32_t count = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if ((count++ % 50) == 0)
        Serial.println("Waiting for commissioning...");
    }
  }

  Serial.println("✅ Matter Thermostat commissioned and ready!");
  Serial.println("Control from your Matter app to see updates.\n");

  thermostatAccessory.begin();
}

void loop() {
  // Simulate temperature (replace with sensor read in real use)
  static float temp = 22.0f;
  static int dir = 1;
  temp += 0.05f * dir;
  if (temp > 25.0f) dir = -1;
  if (temp < 20.0f) dir = 1;
  thermostatAccessory.updateTemperature(temp, NAN);

  // Handle decommission button
  if (digitalRead(BUTTON_PIN) == LOW && !button_state) {
    button_time_stamp = millis();
    button_state = true;
  }
  if (digitalRead(BUTTON_PIN) == HIGH && button_state) {
    button_state = false;
  }

  if (button_state && (millis() - button_time_stamp > DECOMMISSION_TIMEOUT_MS)) {
    Serial.println("Decommissioning Matter Node...");
    Matter.decommission();
    button_time_stamp = millis();
  }

  delay(500);
}
