# Thinknode M1 - MeshCore Firmware

## Overview

The Elecrow Thinknode M1 is an nRF52840-based LoRa device with E-Ink display, designed for low-power mesh networking applications.

## Board Specifications

- **MCU**: Nordic nRF52840 (ARM Cortex-M4F @ 64MHz)
- **Radio**: Semtech SX1262 LoRa transceiver
- **Display**: E-Ink display (compatible with GxEPD2 library)
- **Connectivity**: Bluetooth 5.0 (BLE)
- **Flash**: QSPI Flash storage
- **Battery**: Built-in battery monitoring (ADC)
- **USB**: USB-C for charging and serial communication

## Features

- ✅ **Bluetooth OTA Updates** - Update firmware wirelessly via BLE DFU
- ✅ **Companion Radio** - Connect to MeshCore apps via Bluetooth or USB
- ✅ **Repeater Mode** - Extend mesh network range
- ✅ **Room Server** - Host message servers on the mesh
- ✅ **E-Ink Display** - Low power display for companion radio interface
- ✅ **GPS Support** - Compatible with external GPS modules
- ✅ **Battery Monitoring** - Real-time voltage monitoring

## Firmware Variants

### 1. Companion Radio (BLE)
Connect to Android/iOS MeshCore apps via Bluetooth for messaging and network access.

**Features:**
- Bluetooth Low Energy connectivity
- E-Ink display interface  
- Message queue and contacts
- OTA firmware updates (requires USB for CLI access)
- Battery monitoring

### 2. Companion Radio (USB)
Connect to web-based MeshCore clients via USB serial.

**Features:**
- USB serial connectivity
- Compatible with web clients
- Lower power when not connected
- Serial command interface

### 3. BLE CLI (NEW - Recommended for OTA Updates)
Lightweight firmware providing wireless CLI access via Bluetooth.

**Features:**
- Connect via any Bluetooth terminal app
- Wireless command execution (`start ota`, `info`, `reboot`)
- No USB cable needed for OTA mode
- Fast boot and minimal resource usage
- Perfect for frequent firmware updates

**Usage:**
1. Flash once via USB: `pio run -e ThinkNode_M1_ble_cli -t upload`
2. Device advertises as `THINKNODE_M1_CLI`
3. Connect with BLE terminal app (Serial Bluetooth Terminal, nRF UART, etc.)
4. Type commands: `start ota` to enter OTA mode

### 4. Repeater
Extend mesh network coverage by intelligently forwarding packets.

**Features:**
- Path-aware packet forwarding
- Remote administration via RF
- Configurable flood limits
- Low power operation
- Status LED for transmit activity

### 5. Room Server
Host a message server that stores and forwards messages to users.

**Features:**
- Message history storage (32 messages)
- Multi-user support
- Remote administration
- Guest and admin password protection

## Pin Configuration

```cpp
// LoRa Radio (SX1262)
#define P_LORA_DIO_1    20
#define P_LORA_NSS      24
#define P_LORA_RESET    25
#define P_LORA_BUSY     17
#define P_LORA_SCLK     19
#define P_LORA_MISO     23
#define P_LORA_MOSI     22
#define SX126X_POWER_EN 37

// Status LED
#define P_LORA_TX_LED   13

// Battery Monitoring
#define PIN_VBAT_READ   4
```

## Flashing Firmware

### Via USB (Web Flasher)

1. Visit [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk)
2. Connect Thinknode M1 via USB-C cable
3. Double-click the reset button to enter bootloader mode
4. Select your desired firmware variant
5. Click "Flash" and wait for completion

### Via Bluetooth OTA

See detailed instructions in [thinknode_m1_bluetooth_ota.md](../../docs/thinknode_m1_bluetooth_ota.md)

**Recommended method - Using BLE CLI firmware:**
1. Flash `ThinkNode_M1_ble_cli` firmware once via USB
2. Device advertises as `THINKNODE_M1_CLI`
3. Connect via Bluetooth terminal app
4. Type `start ota` and press Enter
5. Device restarts as `THINKNODE_M1_OTA`
6. Use nRF DFU app to upload firmware ZIP

**Alternative methods:**
- **Companion firmware**: Connect via USB serial, run `start ota` command
- **Repeater/Room Server**: Use MeshCore app remote admin CLI

**Note**: BLE CLI firmware provides the easiest wireless OTA experience without requiring USB cables.

## Configuration

### Setting LoRa Frequency

After flashing, set your regional frequency via serial console:

```bash
set freq 869.618    # For EU868
set freq 915.000    # For US915
```

### Repeater/Room Server Setup

1. Flash repeater or room server firmware
2. Connect via USB and open serial console
3. Configure basic settings:

```bash
set freq 869.618              # Your regional frequency
set name "My Repeater"        # Device name
set lat 51.5074               # GPS latitude
set long -0.1278              # GPS longitude
password mynewpassword        # Change admin password
```

For room servers, also set guest password:
```bash
set guest.password hello123
```

### Battery Voltage Calibration

The Thinknode M1 includes hardware battery monitoring with a 2:1 voltage divider.

**Reading battery voltage:**
- Voltage divider: 150K + 150K resistors (0.5 ratio)
- ADC resolution: 12-bit (0-3.0V range)
- Voltage per LSB: 0.73242188 mV
- Actual voltage: ADC reading × 0.73242188 × 2.0

## Building from Source

### PlatformIO

```bash
# Install PlatformIO
pip install platformio

# Clone repository
git clone https://github.com/meshcore-dev/MeshCore.git
cd MeshCore

# Build BLE CLI firmware (recommended for OTA)
pio run -e ThinkNode_M1_ble_cli

# Build companion firmware (BLE)
pio run -e ThinkNode_M1_companion_radio_ble

# Build companion firmware (USB)
pio run -e ThinkNode_M1_companion_radio_usb

# Build repeater firmware
pio run -e ThinkNode_M1_repeater

# Build room server firmware
pio run -e ThinkNode_M1_room_server

# Upload via USB (example with BLE CLI)
pio run -e ThinkNode_M1_ble_cli -t upload
```

### Custom Builds

Edit `variants/thinknode_m1/platformio.ini` to customize build flags:

```ini
build_flags =
  ${ThinkNode_M1.build_flags}
  -D ADVERT_NAME='"My Custom Device"'
  -D LORA_FREQ=869.618
  -D LORA_TX_POWER=22
```

## Power Management

The Thinknode M1 includes advanced power management features:

### Low Power Modes
- **Active**: ~30-50mA (radio RX/TX)
- **Sleep**: ~200µA (CPU sleep, radio idle)
- **Deep Sleep**: ~50µA (E-Ink refresh off)

### Battery Protection
- Automatic voltage monitoring
- Low battery warnings
- Configurable shutdown voltage
- USB charging support

### Usage Tips
- E-Ink display uses power only during updates
- Bluetooth BLE is optimized for low power
- GPS (if used) should be disabled when not needed

## Troubleshooting

### Device Not Recognized via USB
- Try double-clicking reset button for bootloader mode
- Check USB cable (must support data, not just charging)
- Try different USB port or computer

### Bluetooth OTA Fails
- Ensure battery is charged (>30%)
- Stay within 5 feet during update
- Try reducing packet count to 6 or 8 in nRF DFU app
- Toggle Bluetooth off/on on your phone

### Display Not Working
- Check E-Ink display connection
- Verify correct display class in build flags
- Some display issues resolve after power cycle

### Radio Not Transmitting
- Verify correct frequency is set for your region
- Check SX126X power enable pin (P37) configuration
- Ensure antenna is properly connected

### To Fully Reset Device
1. Download `Flash_erase-nRF52_softdevice_v6.uf2` from flasher
2. Enter bootloader mode (double-click reset)
3. Copy UF2 file to device drive
4. Wait for completion
5. Reflash desired firmware

## Support

- **Documentation**: [MeshCore Wiki](https://github.com/meshcore-dev/MeshCore/wiki)
- **Discord**: [MeshCore Community](https://discord.gg/cYtQNYCCRK)
- **Issues**: [GitHub Issues](https://github.com/meshcore-dev/MeshCore/issues)
- **Manufacturer**: [Elecrow](https://www.elecrow.com/)

## References

- [Main README](../../README.md)
- [MeshCore Documentation](../../docs/docs.md)
- [Bluetooth OTA Guide](../../docs/thinknode_m1_bluetooth_ota.md)
- [Companion Protocol](../../docs/companion_protocol.md)
- [CLI Commands](../../docs/cli_commands.md)

## License

MeshCore firmware is released under the MIT License. See [LICENSE](../../license.txt) for details.
