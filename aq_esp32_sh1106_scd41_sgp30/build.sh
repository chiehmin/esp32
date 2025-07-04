#!/bin/bash

# ESP32 Air Quality Monitor Build Script

echo "ESP32 Air Quality Monitor with SH1106 + SCD41 + SGP30"
echo "======================================================="

# Source ESP-IDF environment
ESP_IDF_PATH="$HOME/esp/esp-idf"
if [ -f "$ESP_IDF_PATH/export.sh" ]; then
    echo "Sourcing ESP-IDF environment..."
    source "$ESP_IDF_PATH/export.sh" > /dev/null 2>&1
    echo "✓ ESP-IDF environment loaded"
else
    echo "✗ ESP-IDF not found at $ESP_IDF_PATH"
    echo "  Please ensure ESP-IDF is installed at ~/esp/esp-idf/"
    echo "  Or update ESP_IDF_PATH in this script"
    exit 1
fi

# Verify idf.py is now available
if ! command -v idf.py &> /dev/null; then
    echo "✗ idf.py still not found after sourcing ESP-IDF"
    echo "  Check your ESP-IDF installation"
    exit 1
fi

echo "✓ idf.py command available"

# Check if wifi credentials exist
if [ ! -f "main/wifi_credentials.h" ]; then
    echo "⚠ WiFi credentials not configured"
    echo "  Copy main/wifi_credentials.h.template to main/wifi_credentials.h"
    echo "  and update with your WiFi SSID and password"
fi

# Set target to ESP32
echo "Setting target to ESP32..."
idf.py set-target esp32

# Update component dependencies
echo "Updating component dependencies..."
idf.py update-dependencies

# Build the project
echo "Building project..."
idf.py build

if [ $? -eq 0 ]; then
    echo "✓ Build successful!"
    echo ""
    echo "To flash and monitor:"
    echo "  idf.py -p /dev/ttyUSB0 flash monitor"
    echo ""
    echo "Features available:"
    echo "  - SH1106 OLED display showing real-time sensor data"
    echo "  - SCD41 sensor: CO2, Temperature, Humidity"
    echo "  - SGP30 sensor: TVOC, equivalent CO2"
    echo "  - WiFi connectivity with HTTP server"
    echo "  - Prometheus metrics at http://[ESP32_IP]/metrics"
else
    echo "✗ Build failed!"
    exit 1
fi
