# ESP32 Air Quality Monitor with SH1106, SCD41, and VOC Sensors

A comprehensive ESP32 air quality monitoring system that combines multiple sensors to provide real-time environmental data.

## Features

- **Multi-Sensor Setup**: 
  - SCD41 for accurate CO2, temperature, and humidity measurements
  - SGP30 for TVOC (Total Volatile Organic Compounds) and equivalent CO2 measurements OR
  - SGP40 for VOC Index using Sensirion's Gas Index Algorithm (0-500 scale)
- **SH1106 OLED Display**: Real-time sensor data visualization
- **WiFi Connectivity**: Wireless network connection for remote monitoring
- **HTTP Server**: Prometheus metrics endpoint for data collection
- **LED Status Indicator**: Visual system status feedback
- **FreeRTOS Tasks**: Multi-threaded operation for optimal performance
- **Automatic Sensor Detection**: Automatically detects which VOC sensor is connected

## Hardware Requirements

- ESP32 development board
- SH1106 OLED display (128x64)
- SCD41 CO2/Temperature/Humidity sensor
- SGP30 TVOC/eCO2 sensor **OR** SGP40 VOC Index sensor
- Breadboard and jumper wires
- USB cable for programming and power

## Pin Configuration

| Component | ESP32 GPIO | Description |
|-----------|------------|-------------|
| LED | GPIO2 | Status indicator LED |
| I2C SDA | GPIO21 | I2C data line for all sensors and display |
| I2C SCL | GPIO22 | I2C clock line for all sensors and display |

### I2C Device Addresses
- SH1106 OLED: 0x3C
- SCD41 Sensor: 0x62 (default)
- SGP30 Sensor: 0x58 (default)
- SGP40 Sensor: 0x59 (default)

## Sensor Specifications

### SCD41 (CO2, Temperature, Humidity)
- CO2 Range: 400-40,000 ppm
- Temperature Range: -10°C to +60°C
- Humidity Range: 0-100% RH
- Update Rate: Every 5 seconds

### SGP30 (TVOC, eCO2)
- TVOC Range: 0-60,000 ppb
- eCO2 Range: 400-60,000 ppm
- Update Rate: Every 1 second
- Warm-up Time: ~12 hours for accurate readings

### SGP40 (VOC Index)
- VOC Index Range: 0-500 (100 = baseline, >100 = worse air quality)
- Update Rate: Every 1 second
- Temperature/Humidity compensation using SCD41 data
- Uses Sensirion's Gas Index Algorithm for stable measurements

## Getting Started

### Prerequisites

1. **ESP-IDF**: Install ESP-IDF development framework v4.4 or later
2. **Configure WiFi**: Copy `main/wifi_credentials.h.template` to `main/wifi_credentials.h` and update with your WiFi credentials

### Building and Flashing

3. **Flash to ESP32**:
   ```bash
   idf.py -p /dev/ttyUSB0 flash
   ```
   Replace `/dev/ttyUSB0` with your actual port (Windows: `COM3`, macOS: `/dev/cu.usbserial-*`)

4. **Monitor serial output**:
   ```bash
   idf.py -p /dev/ttyUSB0 monitor
   ```

### Quick Start with Build Script

You can also use the provided build script:
```bash
./build.sh
```

## Expected Behavior

After flashing and running:

1. **Display Initialization**: SH1106 OLED shows startup messages
2. **Sensor Readings**: Display updates every 2 seconds with:
   - Air Quality title
   - CO2 level from SCD41 (ppm)
   - Temperature and Humidity from SCD41
   - TVOC level from SGP30 (ppb)
3. **LED Status**: Built-in LED blinks every second
4. **WiFi Connection**: Device connects to configured WiFi network
5. **HTTP Server**: Prometheus metrics available at `http://[ESP32_IP]/metrics`

## Display Layout

**With SGP30:**
```
Air Quality
CO2: 450 ppm
T:23.5C H:45.2%
TVOC: 125 ppb
```

**With SGP40:**
```
Air Quality
CO2: 450 ppm
T:23.5C H:45.2%
VOC: 120 idx
```

## Prometheus Metrics

Access metrics at `http://[ESP32_IP]/metrics`:

**With SGP30:**
```
# SCD41 Sensor Metrics
scd41_temperature_celsius 23.50
scd41_humidity_percent 45.20
scd41_co2_ppm 450

# SGP30 Sensor Metrics
sgp30_tvoc_ppb 125
sgp30_eco2_ppm 465

# System Metrics
esp32_uptime_seconds 1234
esp32_free_heap_bytes 245760
```

**With SGP40:**
```
# SCD41 Sensor Metrics
scd41_temperature_celsius 23.50
scd41_humidity_percent 45.20
scd41_co2_ppm 450

# SGP40 Sensor Metrics
sgp40_voc_raw 32456
sgp40_voc_index 120

# System Metrics
esp32_uptime_seconds 1234
esp32_free_heap_bytes 245760
```

## Troubleshooting

### Common Issues

1. **I2C Communication Errors**:
   - Check wiring connections
   - Verify pull-up resistors on SDA/SCL lines
   - Ensure all devices have unique I2C addresses

2. **Sensor Not Responding**:
   - SCD41: Allow 5 seconds for initial reading
   - SGP30: Allow 12+ hours for stable TVOC readings
   - SGP40: Allow 45 seconds for Gas Index Algorithm initialization

3. **VOC Sensor Selection**:
   - Only connect one VOC sensor at a time (SGP30 OR SGP40)
   - The system will automatically detect which sensor is connected
   - Check I2C scan logs to verify sensor detection

3. **Display Issues**:
   - Verify SH1106 I2C address (0x3C)
   - Check display power supply (3.3V)

4. **WiFi Connection Failed**:
   - Verify wifi_credentials.h configuration
   - Check WiFi network availability
   - Monitor serial output for connection status

### Serial Monitor Output

```
I (1234) air_quality_monitor: Starting ESP32 Air Quality Monitor
I (1250) air_quality_monitor: SH1106 OLED display initialized
I (2000) air_quality_monitor: SCD41 sensor initialized
I (2100) air_quality_monitor: SGP30 sensor initialized
I (5000) air_quality_monitor: WiFi connected, IP: 192.168.1.100
```

## Hardware Connections

```
ESP32          SH1106 OLED     SCD41 Sensor    SGP30 Sensor
-----          -----------     ------------    ------------
GPIO21 (SDA) → SDA             SDA             SDA
GPIO22 (SCL) → SCL             SCL             SCL
3.3V         → VCC             VCC             VCC
GND          → GND             GND             GND
```

## License

This project is open source. Feel free to modify and distribute.

```
I (315) ESP32_SAMPLE: Starting ESP32 Sample Application
I (325) ESP32_SAMPLE: ESP-IDF Version: v5.1
I (335) ESP32_SAMPLE: Configuring GPIO pins
I (345) ESP32_SAMPLE: GPIO configuration complete
I (355) ESP32_SAMPLE: LED task started
I (365) ESP32_SAMPLE: Button task started
I (375) ESP32_SAMPLE: Status task started
I (385) ESP32_SAMPLE: All tasks created successfully
I (395) ESP32_SAMPLE: Application initialization complete
I (1405) ESP32_SAMPLE: LED ON
I (2415) ESP32_SAMPLE: LED OFF
I (3425) ESP32_SAMPLE: LED ON
I (4435) ESP32_SAMPLE: Button pressed! Count: 1
```

## Code Structure

- `main.c`: Main application file containing:
  - GPIO configuration
  - LED blinking task
  - Button monitoring task
  - System status reporting task
  - Main application entry point

## Customization

### Changing GPIO Pins

Modify the pin definitions in `main.c`:
```c
#define LED_PIN         GPIO_NUM_2      // Change to your LED pin
#define BUTTON_PIN      GPIO_NUM_0      // Change to your button pin
```

### Adjusting Timing

Modify task delays:
```c
vTaskDelay(pdMS_TO_TICKS(1000));  // LED blink interval (ms)
vTaskDelay(pdMS_TO_TICKS(10000)); // Status report interval (ms)
```

## Troubleshooting

1. **Build errors**: Ensure ESP-IDF is properly installed and sourced
2. **Flash errors**: Check USB cable connection and port permissions
3. **No output**: Verify serial monitor baud rate (115200 by default)
4. **LED not working**: Check if your board has a built-in LED on GPIO2

## Next Steps

This basic project can be extended with:
- Additional sensors (temperature, humidity, etc.)
- WiFi connectivity
- Web server functionality
- OLED/LCD displays
- Motor control
- IoT cloud integration

## Resources

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
