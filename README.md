# IoT Aquarium Water Quality Monitor

An ESP32-based comprehensive water quality monitoring system designed for aquariums. This system continuously reads sensor data, logs historical records, and provides a fully-featured local web dashboard.

## Features
- **Real-Time Web Dashboard**: View live data through an elegant web interface hosted directly on the ESP32.
- **Sensor Integration**: 
  - **Temperature**: DS18B20 digital temperature sensor
  - **Turbidity**: Analog turbidity sensor
  - **pH Level**: Analog pH sensor running through an ADS1115 16-bit ADC for high-precision readings.
- **Historical Data Logging**: Records data to an SD card every 10 minutes, automatically rotating logs and keeping 7 days of history.
- **Interactive Graphs**: The web interface utilizes Chart.js to visualize 7-day historical trends of Temperature, Turbidity, and pH.
- **Configurable Alerts**: Set minimum and maximum thresholds via the web dashboard; alerts are stored in EEPROM and trigger an onboard buzzer.
- **Time Synchronization**: Uses an onboard DS3231 RTC module, auto-syncing with NTP servers whenever WiFi is available.
- **Data Export**: Download CSV logs directly from the dashboard for a single day, the past 7 days, or all historical data.

## Hardware Requirements
- ESP32 Microcontroller
- DS18B20 Temperature Sensor
- Analog Turbidity Sensor
- Analog pH Sensor
- Adafruit ADS1115 16-bit ADC (for the pH sensor)
- 16x2 I2C LCD Display
- DS3231 RTC Module
- SD Card Module
- Active Buzzer

## Installation

1. Open `aquarium-monitor.ino` in the Arduino IDE.
2. Install the necessary libraries:
   - `WebServer`, `EEPROM`, `OneWire`, `DallasTemperature`, `LiquidCrystal_I2C`, `WiFiManager`, `RTClib`, `ArduinoJson`, `NTPClient`, `Adafruit_ADS1X15`
3. Compile and upload to your ESP32.
4. On first boot, connect to the `AquOptimization` WiFi hotspot to configure your local network credentials.
5. The ESP32 will display its IP address on the LCD. Navigate to that IP in your web browser to access the dashboard.

## License
This project is licensed under the [MIT License](LICENSE).
