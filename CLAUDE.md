# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is **BAP Firmware**, a fork of MeshCore firmware customized for the **Heltec Vision Master E290** (2.9" e-paper display LoRa device). The firmware enables these devices to act as:
- MeshCore repeaters for network coverage extension
- Companion clients (BLE/USB) for mobile app connectivity
- Transit information displays for nodes deployed around San Francisco, California

MeshCore is a lightweight C++ library for multi-hop packet routing over LoRa radio, enabling decentralized mesh networks without internet infrastructure.

## Build Commands

### Setup
```bash
# Install PlatformIO in VS Code
# Set firmware version before building
export FIRMWARE_VERSION=v1.0.0
```

### Build Single Target
```bash
# List available targets
sh build.sh list

# Build specific Heltec E290 firmware
sh build.sh build-firmware Heltec_E290_companion_ble_
sh build.sh build-firmware Heltec_E290_companion_usb_
sh build.sh build-firmware Heltec_E290_repeater_
sh build.sh build-firmware Heltec_E290_repeater_bridge_espnow_
sh build.sh build-firmware Heltec_E290_room_server_
```

### Build Multiple Targets
```bash
# Build all firmwares matching a pattern
sh build.sh build-matching-firmwares Heltec_E290

# Build all firmwares by type
sh build.sh build-companion-firmwares
sh build.sh build-repeater-firmwares
sh build.sh build-room-server-firmwares
sh build.sh build-firmwares  # all types
```

### Production Build (No Debug)
```bash
export DISABLE_DEBUG=1
export FIRMWARE_VERSION=v1.0.0
sh build.sh build-firmware Heltec_E290_repeater_
```

### Output Location
Built binaries are placed in `out/` directory:
- ESP32: `.bin` and `-merged.bin` (for fresh install)
- NRF52: `.uf2` and `.zip`
- STM32: `.bin` and `.hex`

### Direct PlatformIO
```bash
# Build with PlatformIO directly
pio run -e Heltec_E290_repeater_

# Upload to connected device
pio run -e Heltec_E290_repeater_ -t upload

# Serial monitor
pio device monitor -p /dev/ttyUSB0 -b 115200
```

## Architecture

### Core Library (`src/`)
The MeshCore library provides the mesh networking foundation:
- `MeshCore.h` - Core constants, `MainBoard` and `RTCClock` interfaces
- `Mesh.cpp/h` - Main mesh networking logic, packet routing
- `Dispatcher.cpp/h` - Packet dispatching to handlers
- `Identity.cpp/h` - Node identity and encryption keys
- `Packet.cpp/h` - Packet structure definitions
- `Utils.cpp/h` - Utility functions

### Platform Helpers (`src/helpers/`)
Platform-specific implementations:
- `esp32/` - ESP32-specific code (BLE, WiFi, OTA updates)
- `nrf52/` - Nordic NRF52 implementations
- `stm32/` - STM32 implementations
- `ui/` - Display drivers (including `E290Display.cpp` for this target)
- `sensors/` - Sensor integration
- `radiolib/` - RadioLib wrappers for LoRa radios
- `bridges/` - Communication bridges (ESPNow, RS232)

### Variants (`variants/`)
Each hardware variant has its own directory with:
- `platformio.ini` - Build configuration for that variant
- `target.cpp/h` - Target-specific initialization
- `*Board.cpp/h` - Board-specific class extending `ESP32Board` or `MainBoard`
- `pins_arduino.h` - Pin mappings (if custom)

The Heltec E290 variant is in `variants/heltec_e290/`:
- `HeltecE290Board.cpp/h` - Board class for power management, battery reading
- Uses `CustomSX1262` radio via SPI
- E-ink display via external library dependency

### Examples (`examples/`)
Firmware applications that link against the core library:
- `companion_radio/` - BLE/USB connectivity for mobile apps, UI support
- `simple_repeater/` - Network range extender, no UI
- `simple_room_server/` - BBS-style shared posts server
- `simple_sensor/` - Remote sensor telemetry
- `simple_secure_chat/` - Terminal-based encrypted messaging
- `kiss_modem/` - Serial KISS protocol for host applications

### Configuration Flow
1. Root `platformio.ini` defines base configurations (`arduino_base`, `esp32_base`, etc.)
2. Variant `platformio.ini` extends base, sets board-specific flags and pins
3. Build flags select which example application to compile via `build_src_filter`
4. Display class, radio driver, and features are configured via `-D` flags

## Key Build Flags

| Flag | Purpose |
|------|---------|
| `Vision_Master_E290` | Identifies E290 hardware variant |
| `DISPLAY_CLASS=E290Display` | E-ink display driver |
| `RADIO_CLASS=CustomSX1262` | LoRa radio driver |
| `LORA_FREQ`, `LORA_BW`, `LORA_SF` | LoRa radio parameters |
| `ADVERT_NAME`, `ADVERT_LAT`, `ADVERT_LON` | Node advertisement |
| `MAX_NEIGHBOURS` | Neighbor table size for repeaters |
| `ADMIN_PASSWORD` | Remote management password |
| `MESH_DEBUG`, `MESH_PACKET_LOGGING` | Debug output (remove for production) |

## Heltec Vision Master E290 Hardware

- **MCU**: ESP32-S3
- **Radio**: SX1262 LoRa (868/915 MHz)
- **Display**: 2.9" e-paper (296x128)
- **Power**: Battery with voltage monitoring on pin 7
- **User Button**: GPIO 0
- **I2C**: SDA=39, SCL=38

## Code Style Guidelines

From the upstream project:
- Keep code simple and concise - think embedded, not high-level
- No dynamic memory allocation except during `setup()`/`begin()` functions
- Follow existing brace and indentation style
- Do not retroactively re-format existing code (creates unnecessary diffs)
- Use `.clang-format` for new code formatting

## Debugging

Enable debug flags in variant `platformio.ini`:
```ini
-D MESH_DEBUG=1
-D MESH_PACKET_LOGGING=1
-D BLE_DEBUG_LOGGING=1
```

Monitor with PlatformIO:
```bash
pio device monitor -b 115200
```

## Documentation

- `docs/faq.md` - Troubleshooting and FAQ
- `docs/packet_format.md` - Mesh packet protocol specification
- `docs/companion_protocol.md` - BLE/USB protocol for mobile apps
- `docs/cli_commands.md` - Serial console commands
