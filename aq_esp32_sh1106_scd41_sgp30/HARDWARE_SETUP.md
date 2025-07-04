ESP32 Air Quality Monitor - Hardware Setup
==========================================

Component List:
- ESP32 Development Board
- SH1106 OLED Display (128x64)
- SCD41 CO2/Temperature/Humidity Sensor
- SGP30 TVOC/eCO2 Sensor
- Breadboard
- Jumper wires

Wiring Diagram:
==============

ESP32 Pin    →    Component
---------          ---------
GPIO21 (SDA) →    SDA (All I2C devices)
GPIO22 (SCL) →    SCL (All I2C devices)
3.3V         →    VCC (All components)
GND          →    GND (All components)
GPIO2        →    Built-in LED (status indicator)

I2C Bus Configuration:
=====================
- SDA: GPIO21
- SCL: GPIO22
- Pull-up resistors: 4.7kΩ (usually built into modules)
- Clock frequency: 100kHz

Device Addresses:
================
- SH1106 OLED: 0x3C
- SCD41 Sensor: 0x62
- SGP30 Sensor: 0x58

Notes:
======
1. All I2C devices share the same SDA and SCL lines
2. Each device must have a unique I2C address
3. 3.3V power supply is sufficient for all components
4. The ESP32 has built-in pull-up resistors for I2C
5. SCD41 requires 5 seconds for initial readings
6. SGP30 needs 12+ hours for stable TVOC measurements

Sensor Specifications:
=====================

SCD41:
- Measurement Range: CO2 400-40,000 ppm, Temp -10°C to +60°C, RH 0-100%
- Accuracy: ±(40 ppm + 5% of reading) for CO2
- Update Rate: 5 seconds
- Power: 3.3V, ~18mA during measurement

SGP30:
- Measurement Range: TVOC 0-60,000 ppb, eCO2 400-60,000 ppm
- Accuracy: ±15% for TVOC, ±15% + 400ppm for eCO2
- Update Rate: 1 second
- Power: 3.3V, ~48mA during measurement

SH1106 OLED:
- Resolution: 128x64 pixels
- Colors: Monochrome (white/blue on black)
- Interface: I2C
- Power: 3.3V, ~20mA
