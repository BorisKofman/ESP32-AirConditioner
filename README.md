# ESP32 Air Conditioner Controller

This project is an implementation of an ESP32-based Air Conditioner controller using the HomeSpan library for HomeKit integration. It uses a DHT11 sensor to monitor temperature and humidity and controls the AC via IR signals.

## Features

- **HomeKit Integration**: Control your air conditioner using Apple's HomeKit.
- **Temperature and Humidity Monitoring**: Uses a DHT11 sensor to report current temperature and humidity.
- **IR Control**: Sends and receives IR signals to control the air conditioner.
- **Persistent Storage**: Uses ESP32's NVS for storing settings.

## Components

- **ESP32**
- **DHT11 Sensor**
- **IR LED and Receiver**
- **HomeSpan Library**

## Circuit Diagram

![Circuit Diagram](./circuit-diagram.png)

## Installation

1. **Clone the repository:**

    ```sh
    git clone https://github.com/yourusername/esp32-air-conditioner-controller.git
    cd esp32-air-conditioner-controller
    ```

2. **Install PlatformIO:**

    Install PlatformIO as an extension in Visual Studio Code.

3. **Install HomeSpan Library:**

    In the `platformio.ini` file, add:

    ```ini
    lib_deps = 
        HomeSpan
        DHT sensor library
        IRremoteESP8266
        Preferences
    ```

4. **Build and Upload:**

    Connect your ESP32 board and use PlatformIO to build and upload the firmware.

## Usage

1. **Connect the DHT11 sensor to pin 21 of the ESP32.**
2. **Connect the IR LED to pin 4 and the IR receiver to pin 14.**
3. **Power the ESP32 and monitor the serial output for the IP address.**
4. **Add the device to HomeKit using the HomeSpan library instructions.**

## Code Overview

Here's a brief overview of the main code components:

- **DHT Sensor Initialization:**

    ```cpp
    #define DHT_PIN 21
    #define DHT_TYPE DHT11

    DHT dht(DHT_PIN, DHT_TYPE);
    ```

- **IR Send and Receive Initialization:**

    ```cpp
    const uint16_t kIrLedPin = 4;
    const uint16_t kRecvPin = 14;

    IRrecv irrecv(kRecvPin);
    IRsend irsend(kIrLedPin);
    ```

- **HomeSpan Service Definition:**

    ```cpp
    class HeaterCooler : public Service::HeaterCooler {
        // Characteristics and methods
    };

    void setup() {
        homeSpan.begin(Category::AirConditioners, "Air Conditioner");
        new HeaterCooler();
    }
    ```

## Contributing

Feel free to fork this project, submit issues, and send pull requests. Contributions are welcome!

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
