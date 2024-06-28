ESP32 HomeKit Air Conditioner Controller

This project implements a HomeKit-enabled Air Conditioner (AC) controller using an ESP32 microcontroller and a DHT11 sensor to measure temperature and humidity. The controller supports various AC modes, including Auto, Heating, and Cooling, and can adjust the fan speed and swing mode.

Features

HomeKit integration using the HomeSpan library.
Temperature and humidity measurement using a DHT11 sensor.
Control of AC modes: Auto, Heating, and Cooling.
Adjustable cooling and heating thresholds.
Adjustable fan speed and swing mode.
State persistence using Non-Volatile Storage (NVS).
Hardware Requirements

ESP32 development board
DHT11 temperature and humidity sensor
AC unit with IR remote control (optional, for full integration)
Breadboard and jumper wires (for prototyping)
Software Requirements

Arduino IDE or PlatformIO
HomeSpan library
DHT sensor library
Adafruit Unified Sensor library
Wiring Diagram

Connect the DHT11 sensor to the ESP32 as follows:

VCC to 3.3V
GND to GND
Data to GPIO 16 (D16 on some boards)
Installation

Clone the repository:

bash
Copy code
git clone https://github.com/your-username/esp32-homekit-ac-controller.git
cd esp32-homekit-ac-controller
Install the required libraries in Arduino IDE:

HomeSpan
DHT sensor library
Adafruit Unified Sensor library
Open the project in Arduino IDE or PlatformIO.

Update WiFi credentials:

Update the setup function with your WiFi credentials:

cpp
Copy code
homeSpan.setWifiCredentials("YOUR-SSID", "YOUR-PASSWORD");
Upload the code to your ESP32:

Select the appropriate board and port in the Arduino IDE and upload the code.

Code Explanation

ACController Class
The ACController class extends the Service::HeaterCooler class provided by HomeSpan. It defines various characteristics and handles the state and behavior of the AC unit.

Main Functions
setup(): Initializes the HomeKit accessory, DHT sensor, and sets up the ACController instance.
loop(): Continuously polls HomeSpan and updates temperature and humidity readings.
Key Methods
update(): Handles changes to the AC state and prints relevant updates to the serial monitor.
readTemperatureAndHumidity(): Reads temperature and humidity values from the DHT11 sensor and updates the HomeKit characteristics.
Usage

After uploading the code and connecting to your WiFi network, you can pair the ESP32 with the Apple Home app. The device will appear as an Air Conditioner with adjustable settings for mode, temperature, fan speed, and swing mode.

Example Output

When the state or temperature settings change, the serial monitor will display messages like:

sql
Copy code
Update called
Turn on AC: 1
heatingTemp: 27.0
Troubleshooting

Ensure the DHT11 sensor is connected correctly.
Verify your WiFi credentials are correct.
Use the serial monitor to debug and ensure the ESP32 is connected to the WiFi and HomeKit.
Contributing

Contributions are welcome! Please fork the repository and create a pull request with your changes.

License

This project is licensed under the MIT License. See the LICENSE file for details.