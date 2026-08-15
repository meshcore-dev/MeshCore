# M5Stack Unit C6L - MeshCore Variant

## Overview

Compact ESP32-C6 LoRa module with integrated SX1262 radio, 64x48 OLED display, and PI4IO I/O expander.

## Hardware

- **MCU:** ESP32-C6 (WiFi 6 + Bluetooth 5 LE)
- **LoRa:** SX1262 (SPI)
- **Display:** 64x48 SSD1306 OLED (SPI)
- **I/O Expander:** PI4IO (I2C)
- **LED:** NeoPixel (GPIO 2)
- **Buzzer:** GPIO 11

## Pin Configuration

| Function | GPIO |
|----------|------|
| LoRa SCLK | 20 |
| LoRa MISO | 22 |
| LoRa MOSI | 21 |
| LoRa NSS | 23 |
| LoRa DIO1 | 7 |
| Display CS | 6 |
| Display DC | 18 |
| Display RST | 15 |
| I2C SDA | 10 |
| I2C SCL | 8 |
| NeoPixel | 2 |
| Buzzer | 11 |
| GPS RX | 4 |
| GPS TX | 5 |

## Firmware Environments

| Environment | Interface | Use Case |
|-------------|-----------|----------|
| `m5stack_unit_c6l_companion_radio_usb` | USB Serial | Connect via USB |
| `m5stack_unit_c6l_companion_radio_ble` | BLE | Connect via Bluetooth |
| `m5stack_unit_c6l_repeater` | LoRa | Network extender |
| `m5stack_unit_c6l_room_server` | LoRa | BBS server |
| `m5stack_unit_c6l_kiss_modem` | USB Serial | KISS TNC |

## Build

```bash
pio run -e m5stack_unit_c6l_companion_radio_usb
```

## Resources

- [M5Stack Unit C6L Product Page](https://docs.m5stack.com/en/unit/Unit_C6L)
- [MeshCore Documentation](https://docs.meshcore.io)