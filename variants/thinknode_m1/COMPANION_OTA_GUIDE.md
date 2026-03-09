# ThinkNode M1 Companion with OTA Update Support

## Overview

This firmware variant combines the full functionality of the ThinkNode M1 BLE Companion radio with the ability to wirelessly trigger firmware updates. It provides a simple button-based mechanism to enter OTA mode without needing USB connection.

## Features

- **Full BLE Companion Radio Functionality**: Chat, contacts, groups, GPS, display, sensors
- **Simple OTA Trigger**: Hold the USER button for 5 seconds to enter OTA mode
- **No Configuration Issues**: Based on the proven BLE companion firmware
- **All Original Features**: Maintains complete compatibility with companion radio features

## Hardware Requirements

- ThinkNode M1 board with BLE support
- USER button (GPIO pin 42 - main button on board)
- nRF Device Firmware Update app installed on mobile device
  - iOS: "nRF Device Firmware Update" by Nordic Semiconductor ASA
  - Android: "nRF Device Firmware Update (DFU)" by Nordic Semiconductor ASA

## Firmware Build and Flash

### Building the Firmware

```powershell
# From the MeshCore root directory
pio run -e ThinkNode_M1_companion_ota
```

### Flashing via USB

```powershell
pio run -e ThinkNode_M1_companion_ota -t upload
```

## Usage

### Initial Setup

1. Flash the companion_ota firmware to your ThinkNode M1
2. Power on the device
3. Connect via BLE using your companion app (use PIN: 123456)
4. Use the device normally for messaging, GPS, etc.

### Triggering OTA Mode

When you want to update the firmware:

1. **Press and hold the USER button** (main button on the board)
2. Wait 5 seconds while holding the button
3. Serial output will show:
   ```
   USER button pressed - hold for 5 seconds to enter OTA mode
   =========================================
   OTA TRIGGER! Entering OTA mode...
   =========================================
   Entering OTA mode...
   ```
4. The device will disconnect from companion app and enter DFU bootloader mode
5. The device will advertise as "THINKNODE_M1_OTA" via BLE

### Performing the Update

1. Open the **nRF Device Firmware Update** app on your phone
2. Find **"THINKNODE_M1_OTA"** in the list of available devices
3. Tap on it to connect
4. Select the firmware file (.zip or .hex) you want to flash
5. Tap "Upload" to start the firmware update
6. Wait for the update to complete (progress will be shown)
7. Device will automatically reboot with the new firmware

### Preparing Firmware Files

The firmware update requires a properly formatted file:

#### Option 1: UF2 File (Converted to ZIP)

```powershell
# Build your target firmware
pio run -e ThinkNode_M1_companion_ota

# The UF2 file will be at:
.pio\build\ThinkNode_M1_companion_ota\firmware.uf2

# For nRF DFU, you need to package it as a ZIP with Nordic's tools
# or use the HEX file directly (Option 2)
```

#### Option 2: HEX File (Recommended)

```powershell
# The build also creates a HEX file:
.pio\build\ThinkNode_M1_companion_ota\firmware.hex

# Package it for DFU using nrfutil:
nrfutil pkg generate --hw-version 52 --sd-req 0xCB --application-version 1 --application .pio\build\ThinkNode_M1_companion_ota\firmware.hex --key-file private.pem firmware_update.zip
```

**Note:** For the above command to work, you need:
- `nrfutil` installed (`pip install nrfutil`)
- A private key file (can be generated with `nrfutil keys generate private.pem`)

## Technical Details

### Button Configuration

- **Button Pin**: GPIO 42 (PIN_BUTTON1)
- **Button Logic**: Active LOW (pressed = LOW)
- **Hold Duration**: 5000ms (5 seconds)

### OTA Trigger Process

1. Firmware monitors USER button state in the main loop
2. When pressed, starts a timer
3. If held for 5+ seconds, calls `board.startOTAUpdate("THINKNODE_M1_OTA", reply)`
4. This triggers the Adafruit DFU bootloader
5. Bootloader advertises as "THINKNODE_M1_OTA" and waits for DFU connection

### Build Configuration

The `ThinkNode_M1_companion_ota` build target is configured in `platformio.ini`:

```ini
[env:ThinkNode_M1_companion_ota]
extends = ThinkNode_M1
board_build.ldscript = boards/nrf52840_s140_v6_extrafs.ld
build_flags =
  ${ThinkNode_M1.build_flags}
  -D USER_BTN_PRESSED=LOW
  -D BLE_PIN_CODE=123456
  ...other flags...
build_src_filter = 
  -<../examples/companion_radio/main.cpp>
  +<companion_with_ota.cpp>
```

Key differences from standard companion:
- Uses `companion_with_ota.cpp` instead of standard companion main.cpp
- Adds button monitoring code to the main loop
- Includes serial output for OTA trigger feedback

## Troubleshooting

### Button Not Working

- **Check button pin**: Verify GPIO 42 is the correct USER button on your board
- **Test button physically**: Press the button and check if LED responds
- **Serial monitoring**: Connect via USB and monitor serial output to see button press messages

### Can't Find "THINKNODE_M1_OTA" Device

- **Wait for bootloader**: It takes a few seconds after button press
- **Check Bluetooth**: Make sure phone Bluetooth is on and nRF DFU app has permissions
- **Power cycle**: Try resetting the device and triggering OTA again
- **Check serial output**: Verify "Entering OTA mode" message appeared

### Update Fails

- **File format**: Ensure you're using a properly packaged DFU ZIP file
- **Battery level**: Low battery can cause update failures - charge device first
- **BLE range**: Stay close to device during update
- **Retry**: If update fails, device will remain in DFU mode - just retry the update

### Device Won't Boot After Update

- **Enter bootloader manually**: Double-tap the RESET button quickly
- **Flash via USB**: Connect USB and flash known-good firmware
- **Check firmware compatibility**: Ensure the firmware was built for ThinkNode M1

## Comparison with Other Firmwares

| Firmware Variant | OTA Trigger | CLI Access | Companion Features | Configuration |
|-----------------|-------------|------------|-------------------|--------------|
| **companion_ota** | ✅ Button (5s hold) | ❌ No | ✅ Full | ✅ Stable |
| ble_cli | ✅ CLI command | ✅ BLE CLI | ❌ No companion | ⚠️ Config issues |
| repeater | ✅ CLI command | ✅ USB CLI | ❌ No companion | ✅ Stable |
| companion_ble | ❌ No OTA | ❌ No | ✅ Full | ✅ Stable |

## Source Code

The firmware is implemented in:
- `variants/thinknode_m1/companion_with_ota.cpp` - Main firmware file
- `variants/thinknode_m1/platformio.ini` - Build configuration

## License

Same as MeshCore project license.
