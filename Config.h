#ifndef CONFIG_H
#define CONFIG_H

// Transport select: 0 = Matter over WiFi (Arduino IDE build),
//                   1 = Matter over Thread (Arduino-as-IDF-component build).
// Thread also requires disabling Matter's WiFi station in sdkconfig and a
// Thread Border Router on the network. See THREAD_SETUP.md.
#define USE_THREAD     1

// WiFi (ignored when USE_THREAD == 1)
#define WIFI_SSID      "kofman_IoT"
#define WIFI_PASSWORD  ""

// NTP (background only, never blocks boot)
#define NTP_SERVER_1           "pool.ntp.org"
#define NTP_SERVER_2           "time.google.com"

// Matter pairing identity - make unique per device
// Passcode: 8 digits, no trivial sequences; discriminator: 0..4095
#define MATTER_PASSCODE        45822673
#define MATTER_DISCRIMINATOR   101

// AC setpoint range (C) - matches Goodweather protocol (16..31)
#define AC_MIN_TEMP_C          16
#define AC_MAX_TEMP_C          31

// Temperature/humidity sensor
#define DHTPIN                 16
#define DHTTYPE                DHT22   // or DHT11
#define TEMP_OFFSET            2       // degrees C subtracted (self-heating)
#define SENSOR_READ_MS         30000

// Send IR only after the dial stops moving this long
#define AC_SEND_DEBOUNCE_MS    1200

// IR transceiver (IRremoteESP8266)
#define IR_SEND_PIN            4
#define IR_RECV_PIN            15
#define IR_CAPTURE_BUFFER      2048
#define IR_TIMEOUT_MS          15
#define IR_LOG_ALL_SIGNALS     false   // true = log noise frames too (RX debug)
// What thermostat AUTO sends to the AC (kFan = fan-only, kAuto = AC's auto)
#define AUTO_MODE              stdAc::opmode_t::kFan

// Factory reset button: hold at power-on or long-press at runtime
#define RESET_BUTTON_PIN       0      // BOOT button
#define RESET_HOLD_MS          5000

// Serial output switches
#define SHOW_PAIRING_CODE      true
#define SHOW_WIFI_STATUS       true
#define SHOW_MATTER_STATUS     true

#endif  // CONFIG_H
