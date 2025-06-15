| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- |

# ESP32 Air Quality Monitor

This project implements an air quality monitoring system using ESP32. It combines several sensors and a display to provide real-time environmental data monitoring:

- CO2 and TVOC measurements using SGP30 sensor
- Temperature and humidity monitoring using DHT11 sensor
- SH1106 OLED display for data visualization
- WiFi connectivity for potential remote monitoring
- Onboard LED for status indication

All components are managed via ESP-IDF's component manager.

## How to Use Example

Before project configuration and build, be sure to set the correct chip target using `idf.py set-target <chip_name>`.

### Hardware Required

* ESP32 development board (ESP32-S3 recommended)
* SGP30 Air Quality Sensor
* DHT11 Temperature and Humidity Sensor
* SH1106 OLED Display (128x64)
* USB cable for power and programming

#### Pin Connections

* **I2C Bus**
  - SDA: GPIO8
  - SCL: GPIO9
  - Used by both SGP30 and SH1106 display
* **DHT11**
  - Data: GPIO5
* **LED**
  - Built-in addressable LED (configured via menuconfig)

See [Development Boards](https://www.espressif.com/en/products/devkits) for more information about ESP32 development boards.

### Configure the Project

1. **WiFi Configuration**
   - Copy `main/wifi_credentials.h.template` to `main/wifi_credentials.h`
   - Edit `wifi_credentials.h` with your WiFi credentials:
     ```c
     #define WIFI_SSID       "your_wifi_ssid"
     #define WIFI_PASS       "your_wifi_password"
     ```
   Note: `wifi_credentials.h` is ignored by git to keep credentials secure

2. **Project Configuration**
   Open the project configuration menu (`idf.py menuconfig`):

   * In the `Example Configuration` menu:
     - Configure the onboard LED type and GPIO
     - Adjust other project-specific settings if needed

### Build and Flash

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Operation

When running, the device will:

1. Initialize all sensors and the display
2. Connect to configured WiFi network
3. Display real-time measurements on the OLED screen:
   - Line 1: CO2 levels (ppm)
   - Line 2: TVOC levels (ppb)
   - Line 3: Temperature and Humidity
   - Line 4: WiFi status and IP address

The onboard LED blinks to indicate the device is operating normally.

Example output in terminal:
```text
I (315) main: Example configured to blink addressable LED!
I (325) main: Initialize I2C
I (332) main: Found I2C device at address 0x3C
I (332) main: Found I2C device at address 0x58
I (342) main: Initialize OLED panel
I (349) main: OLED display initialized and cleared
I (355) main: Initializing WiFi
I (362) main: WiFi initialization completed
I (1325) main: Connected! IP: 192.168.1.100
I (2325) main: CO2: 400 ppm, TVOC: 0 ppb
I (2330) main: Temp: 25.0°C, Humidity: 60.0%
```

Note: The color order could be different according to the LED model.

The pixel number indicates the pixel position in the LED strip. For a single LED, use 0.

## Troubleshooting

* If sensors aren't detected, verify I2C connections and addresses:
  - SGP30 should be at address 0x58
  - SH1106 should be at address 0x3C
* If DHT11 readings fail, check GPIO connection and pull-up resistor
* If WiFi connection fails:
  - Verify credentials in `wifi_credentials.h`
  - Check if the network is within range
  - Ensure the network supports WPA2-PSK
* If display is not working:
  - Verify I2C connections
  - Check contrast settings
  - Make sure the display is properly powered

## Data Visualization

This project supports data visualization using Prometheus and Grafana. The ESP32 exposes metrics in Prometheus format at the `/metrics` endpoint.

### Setup Prometheus and Grafana

1. Install Docker and Docker Compose if you haven't already
2. Update the ESP32's IP address in `prometheus.yml`:
   ```yaml
   static_configs:
     - targets: ['your-esp32-ip:80']
   ```
3. Start Prometheus and Grafana:
   ```bash
   docker-compose up -d
   ```
4. Access Grafana at http://localhost:3000
   - Default credentials: admin/admin
   - Add Prometheus as a data source:
     - URL: http://prometheus:9090
     - Access: Browser
5. Import the provided dashboard:
   - Go to Dashboards → Import
   - Upload the `grafana-dashboard.json` file

### Available Metrics

The following metrics are available:
- `air_quality_co2_ppm` - CO2 concentration in parts per million
- `air_quality_tvoc_ppb` - Total Volatile Organic Compounds in parts per billion
- `air_quality_temperature_celsius` - Temperature in degrees Celsius
- `air_quality_humidity_percent` - Relative humidity percentage
- `air_quality_last_update_timestamp` - Unix timestamp of last sensor update

### Troubleshooting Visualization

* If metrics aren't showing up:
  - Verify the ESP32's IP address in `prometheus.yml`
  - Check if the `/metrics` endpoint is accessible
  - Look for errors in Prometheus logs: `docker-compose logs prometheus`
* If graphs aren't appearing in Grafana:
  - Verify Prometheus data source is working
  - Check the time range selection in Grafana
  - Ensure metrics are being collected in Prometheus

## Components Used

* [SGP30](https://github.com/chiehmin/esp32-sgp30) - Air quality sensor driver
* [DHT11](https://github.com/AchimPieters/esp32-dht) - Temperature/humidity sensor driver
* [SH1106](https://github.com/TNY-Robotics/sh1106-esp-idf) - OLED display driver
* [LED Strip](https://components.espressif.com/component/espressif/led_strip) - LED control library

For technical queries about:
* ESP32 and ESP-IDF: Open an [issue on ESP-IDF](https://github.com/espressif/esp-idf/issues)
* This project: Open an issue in this repository
