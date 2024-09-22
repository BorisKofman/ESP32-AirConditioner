#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>  // Include this line to define fixed-width integer types

#define USE_PIXEL 1  
#define STATUS_LED_PIN 48

#define SEND_PIN 4
#define RECV_PIN 15
#define BAUD_RATE 115200
#define CAPTURE_BUFFER_SIZE 2048
#define TIMEOUT 15

// #define USE_BME680
#define DHTPIN 16     // DHT sensor pin
#define DHTTYPE DHT22   // DHT sensor type
#define TEMP_OFFSET 3.0 

// #define USE_LD2410 
// #define USE_LD2412 
const int rxPin = 43;
const int txPin = 44;
// #define DEBUG 

// Uncomment the following line to enable iBeacon functionality
#define BEACON
// iBeacon parameters
#define BEACON_UUID "e2c56db5-dffb-48d2-b060-d0f5a71096e0"
static const uint16_t major = 1;
static const uint16_t minor = 1;
static const int txPower = -59;

#endif