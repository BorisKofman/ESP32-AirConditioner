#ifndef CONFIG_H
#define CONFIG_H


#define USE_PIXEL 1  
#define STATUS_LED_PIN 48

#define SEND_PIN 4
#define RECV_PIN 15
#define BAUD_RATE 115200
#define CAPTURE_BUFFER_SIZE 2048
#define TIMEOUT 15

#define USE_BME680 1
#define DHTPIN 16     // DHT sensor pin
#define DHTTYPE DHT22   // DHT sensor type

#define USE_LD2412 = 1
const int rxPin = 43;
const int txPin = 44;
#define DEBUG = 0

#endif