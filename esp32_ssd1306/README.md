# ESP32 Simple Sample Project

A basic ESP32 project demonstrating fundamental embedded programming concepts using the ESP-IDF framework.

## Features

- **LED Control**: Automatic LED blinking every second
- **Button Input**: Detects button presses with debouncing
- **Serial Output**: Comprehensive logging and status information
- **FreeRTOS Tasks**: Multiple concurrent tasks for different functions
- **System Monitoring**: Periodic system status reports

## Hardware Requirements

- ESP32 development board (ESP32-DevKitC, ESP32-WROOM, etc.)
- USB cable for programming and power
- Built-in LED (GPIO2) and boot button (GPIO0) are used

## Pin Configuration

| Component | GPIO Pin | Description |
|-----------|----------|-------------|
| LED | GPIO2 | Built-in LED on most ESP32 boards |
| Button | GPIO0 | Boot button (pull-up enabled) |

## Getting Started

### Prerequisites

1. **ESP-IDF**: Install ESP-IDF development framework
   ```bash
   # Follow the official ESP-IDF installation guide
   # https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/
   ```

2. **Configure ESP-IDF environment**:
   ```bash
   # Source the ESP-IDF environment (add to ~/.bashrc for permanent)
   . $HOME/esp/esp-idf/export.sh
   ```

### Building and Flashing

1. **Set target chip**:
   ```bash
   idf.py set-target esp32
   ```

2. **Configure project** (optional):
   ```bash
   idf.py menuconfig
   ```

3. **Build the project**:
   ```bash
   idf.py build
   ```

4. **Flash to ESP32**:
   ```bash
   idf.py -p /dev/ttyUSB0 flash
   ```
   Replace `/dev/ttyUSB0` with your actual port (Windows: `COM3`, macOS: `/dev/cu.usbserial-*`)

5. **Monitor serial output**:
   ```bash
   idf.py -p /dev/ttyUSB0 monitor
   ```

## Expected Behavior

After flashing and running:

1. **LED Blinking**: The built-in LED will blink every second
2. **Button Response**: Press the boot button to increment a counter
3. **Serial Output**: View logs showing:
   - LED state changes
   - Button press counts
   - System status every 10 seconds
   - Memory usage and uptime

## Sample Output

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
