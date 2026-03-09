# Bluetooth CLI Implementation for Thinknode M1 - Summary

## What Was Implemented

I've added **complete Bluetooth CLI access** to the Thinknode M1, allowing you to wirelessly trigger OTA firmware updates without needing a USB cable.

## New Files Created

### 1. BLE CLI Firmware (`variants/thinknode_m1/ble_cli_main.cpp`)
- Lightweight standalone firmware
- Provides wireless CLI via Bluetooth
- Advertises as `THINKNODE_M1_CLI`
- Supports commands: `start ota`, `info`, `help`, `reboot`
- Works with any Bluetooth terminal app

### 2. BLE CLI Library (Optional, for Advanced Use)
- `src/helpers/nrf52/BLECLIInterface.h`
- `src/helpers/nrf52/BLECLIInterface.cpp`
- Can be integrated into other firmwares if needed

### 3. Build Configuration
- Added to `variants/thinknode_m1/platformio.ini`
- Build target: `ThinkNode_M1_ble_cli`

### 4. Documentation
- **[BLE CLI Quick Start Guide](variants/thinknode_m1/BLE_CLI_GUIDE.md)** - Step-by-step usage instructions
- **[Bluetooth OTA Guide](docs/thinknode_m1_bluetooth_ota.md)** - Complete OTA update procedures
- **[Thinknode M1 README](variants/thinknode_m1/README.md)** - Board documentation with all firmware variants

## How to Use

### One-Time Setup
```bash
# Build and flash BLE CLI firmware via USB
cd MeshCore
pio run -e ThinkNode_M1_ble_cli -t upload
```

### Wireless OTA Updates (No USB Needed!)
1. **Connect** to `THINKNODE_M1_CLI` using a Bluetooth terminal app
2. **Type**: `start ota` and press Enter
3. **Device reboots** and advertises as `THINKNODE_M1_OTA`
4. **Use nRF DFU app** to flash your desired firmware ZIP
5. **Done!** Device reboots with new firmware

### Available Bluetooth Terminal Apps
- **Android**: Serial Bluetooth Terminal, BlueTerm, nRF Connect (UART)
- **iOS**: nRF UART, BlueTerm, LightBlue

## Commands Available via BLE CLI

| Command | Description |
|---------|-------------|
| `start ota` | Enter OTA firmware update mode |
| `info` | Show device info (battery, MAC address, etc.) |
| `help` | Display command list |
| `reboot` | Reboot device |

## Complete Workflow Example

```
1. Flash BLE CLI once via USB:
   $ pio run -e ThinkNode_M1_ble_cli -t upload

2. Future updates are wireless:
   - Connect via Bluetooth to THINKNODE_M1_CLI
   - Type: start ota
   - Use nRF DFU app to flash firmware

3. To update again:
   - Flash back to BLE CLI via OTA
   - Repeat step 2
```

## Benefits

✅ **Fully Wireless** - No USB cable needed after initial setup
✅ **Simple** - Just 4 commands to remember
✅ **Fast** - Lightweight firmware boots quickly
✅ **Universal** - Works with any Bluetooth terminal app
✅ **Flexible** - Easy to switch between firmware variants

## Build Targets

```bash
# BLE CLI firmware (for wireless OTA triggers)
pio run -e ThinkNode_M1_ble_cli

# Companion radios
pio run -e ThinkNode_M1_companion_radio_ble
pio run -e ThinkNode_M1_companion_radio_usb

# Mesh infrastructure
pio run -e ThinkNode_M1_repeater
pio run -e ThinkNode_M1_room_server
```

## Technical Details

- **Device Name (CLI mode)**: `THINKNODE_M1_CLI`
- **Device Name (OTA mode)**: `THINKNODE_M1_OTA`
- **BLE Service**: Nordic UART Service (NUS)
- **Serial Settings**: 115200 baud (also works via USB for debugging)
- **Base Class**: Inherits from `NRF52Board` for OTA support
- **Dependencies**: Uses Adafruit Bluefruit library

## Files Modified/Created

```
variants/thinknode_m1/
  ├── ble_cli_main.cpp              [NEW - Main firmware]
  ├── ble_cli_support.cpp           [NEW - Helper (not currently used)]
  ├── BLE_CLI_GUIDE.md              [NEW - Quick start guide]
  ├── README.md                     [UPDATED - Added BLE CLI info]
  └── platformio.ini                [UPDATED - Added build target]

src/helpers/nrf52/
  ├── BLECLIInterface.h             [NEW - Library header]
  └── BLECLIInterface.cpp           [NEW - Library implementation]

docs/
  └── thinknode_m1_bluetooth_ota.md [UPDATED - Added BLE CLI method]
```

## Next Steps

1. **Build the firmware**:
   ```bash
   pio run -e ThinkNode_M1_ble_cli
   ```

2. **Flash via USB** (one time):
   ```bash
   pio run -e ThinkNode_M1_ble_cli -t upload
   ```

3. **Test it**:
   - Download a Bluetooth terminal app
   - Connect to `THINKNODE_M1_CLI`
   - Type `help` to see commands
   - Type `info` to see device information
   - Type `start ota` to test OTA mode

4. **Flash different firmware via OTA**:
   - Download firmware ZIP from flasher.meshcore.co.uk
   - Use nRF DFU app to upload

## Backwards Compatibility

All existing firmware variants continue to work unchanged:
- Companion radios still require USB for CLI access
- Repeaters/Room servers still have remote admin CLI
- BLE CLI is an additional option, not a replacement

## Support

For detailed instructions, see:
- [BLE CLI Quick Start](variants/thinknode_m1/BLE_CLI_GUIDE.md)
- [Bluetooth OTA Full Guide](docs/thinknode_m1_bluetooth_ota.md)
- [Board Documentation](variants/thinknode_m1/README.md)
