# Thinknode M1 Bluetooth OTA Update Guide

## Overview

The Thinknode M1 board has built-in support for Bluetooth Over-The-Air (OTA) firmware updates using the Nordic nRF52840's Device Firmware Update (DFU) protocol. This allows you to wirelessly update the firmware without needing a USB connection.

## Prerequisites

- Thinknode M1 board with MeshCore firmware installed
- Smartphone (Android or iOS) with Bluetooth
- **nRF Device Firmware Update** app installed:
  - [iOS App Store](https://apps.apple.com/app/nrf-device-firmware-update/id1624454660)
  - [Google Play Store](https://play.google.com/store/apps/details?id=no.nordicsemi.android.dfu)
- Firmware ZIP file for Thinknode M1 (download from [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk))

## Hardware Configuration

The Thinknode M1 is configured with the following OTA-capable components:

- **MCU**: nRF52840
- **Bootloader**: Adafruit nRF52 Bootloader with DFU support
- **SoftDevice**: S140 v6.1.1
- **Bluetooth**: BLE 5.0
- **OTA Name**: `THINKNODE_M1_OTA`

## How to Update Firmware via Bluetooth

### Step 1: Download Firmware

1. Visit [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk)
2. Find the Thinknode M1 section
3. Download the **ZIP** version of your desired firmware (companion, repeater, or room server)
4. Transfer the ZIP file to your smartphone

### Step 2: Enable OTA Mode

**Method 1: Using BLE CLI Firmware (RECOMMENDED - Fully Wireless)**

The easiest way to enable OTA mode wirelessly is to flash the special BLE CLI firmware:

1. Flash the `ThinkNode_M1_ble_cli` firmware to your device (via USB one time)
2. The device will advertise as `THINKNODE_M1_CLI`
3. Connect using a Bluetooth terminal app:
   - **Android**: Serial Bluetooth Terminal, BlueTerm
   - **iOS**: nRF UART, BlueTerm
   - Or use nRF Connect app → UART service
4. Type `start ota` and press Enter
5. Device will restart and advertise as `THINKNODE_M1_OTA`
6. Proceed to Step 3 to flash your desired firmware

**Method 2: For Companion Radio (BLE or USB) - Via USB Serial**

The companion firmware does not expose a command line interface through the MeshCore app. You need to use a USB serial connection:

1. Connect your Thinknode M1 to a computer via USB-C cable
2. Open the [MeshCore Web Flasher](https://flasher.meshcore.co.uk)
3. Click **Console** and select the serial port for your device
4. Type `start ota` and press Enter
5. You should see `OK` confirmation
6. Disconnect the USB cable
7. The device will restart in OTA mode and be discoverable as `THINKNODE_M1_OTA`

**Alternative USB Serial Tools:**

Some users may prefer to use a serial terminal application:
- **Windows**: PuTTY, TeraTerm, or Arduino Serial Monitor
- **Mac/Linux**: screen, minicom, or Arduino Serial Monitor
- **Settings**: 115200 baud, 8N1

**Method 3: For Repeater/Room Server (Remote Administration)**

If you have remote administration access (via T-Deck or MeshCore app):

1. Connect to the repeater/room server remotely via the MeshCore app
2. Login with admin credentials
3. Go to the Command Line tab
4. Type `start ota` and press Enter
5. Wait for confirmation that OTA mode is enabled
6. The device will restart in OTA mode and be discoverable as `THINKNODE_M1_OTA`

### Step 3: Flash Firmware with nRF DFU App

1. Open the **nRF Device Firmware Update** app on your smartphone
2. Tap the **Settings** icon (gear) in the top right corner
3. Enable **Packets receipt notifications**
4. Set **Number of Packets** to `8` (recommended for Thinknode M1)
   - You can try values between 8-10 if you experience issues
5. Go back to the main screen
6. Tap **Select Device**
7. Look for `THINKNODE_M1_OTA` in the list of available devices
   - If not found, enable **Force Scanning** in Settings
8. Select `THINKNODE_M1_OTA`
9. Tap **Select File** and choose the firmware ZIP you downloaded
10. Tap **Upload** to begin the update
11. Wait for the update to complete (typically 2-5 minutes)
12. The device will automatically reboot with the new firmware

## Troubleshooting

### Device Not Found
- Ensure the device entered OTA mode successfully (check for `OK` response)
- Make sure Bluetooth is enabled on your smartphone
- Try toggling Bluetooth off and on
- Enable **Force Scanning** in the nRF DFU app settings
- Move closer to the device (within 3-5 feet)

### Update Fails or Times Out
- Try reducing the **Number of Packets** value (try 6 or 4)
- Ensure the device has sufficient battery (above 30%)
- Restart your smartphone's Bluetooth
- Reboot your smartphone if issues persist
- Make sure you're using the correct firmware ZIP for Thinknode M1

### Device Won't Enter OTA Mode
- **For Companion Radio**: Make sure you're using a USB serial connection to send the `start ota` command, not the MeshCore app
- **For Repeater/Room Server**: Check that you have admin access and are logged in remotely
- Verify you have the correct firmware version that supports the `start ota` command
- Try resetting the device and attempting again
- Check serial console for error messages

### Update Completes but Device Doesn't Boot
- The enhanced DFU bootloader will automatically fall back to OTA mode if firmware is invalid
- Try flashing again via Bluetooth
- If still failing, connect via USB and use the web flasher

## Technical Details

### OTA Update Process

When you execute the `start ota` command:

1. The device configures Bluetooth with maximum bandwidth
2. Bluefruit library initializes with optimized connection parameters
3. BLE DFU service starts and becomes discoverable
4. The device advertises as `THINKNODE_M1_OTA`
5. The nRF DFU app connects and transfers the new firmware
6. The bootloader validates and installs the firmware
7. The device reboots automatically with the new firmware

### Board Configuration

The Thinknode M1 board JSON includes these OTA-critical settings:

```json
{
  "connectivity": ["bluetooth"],
  "upload": {
    "protocol": "nrfutil",
    "protocols": ["jlink", "nrfjprog", "nrfutil", "stlink"],
    "use_1200bps_touch": true,
    "require_upload_port": true,
    "wait_for_upload_port": true
  },
  "bootloader": {
    "settings_addr": "0xFF000"
  },
  "softdevice": {
    "sd_name": "s140",
    "sd_version": "6.1.1"
  }
}
```

### Code Implementation

The Thinknode M1 inherits OTA functionality from the `NRF52Board` base class:

```cpp
class ThinkNodeM1Board : public NRF52Board {
public:
  ThinkNodeM1Board() : NRF52Board("THINKNODE_M1_OTA") {}
  // ... other methods
};
```

The `startOTAUpdate()` method is implemented in `NRF52Board.cpp` and handles all BLE DFU initialization.

## BLE CLI Firmware

For users who frequently update firmware via Bluetooth OTA, a special lightweight **BLE CLI firmware** is available. This firmware provides wireless CLI access without needing USB connections.

### Features:
- Connect via Bluetooth terminal app
- Issue commands wirelessly: `start ota`, `info`, `reboot`
- Lightweight and fast boot
- Perfect for OTA updates

### Building BLE CLI Firmware:

```bash
# Using PlatformIO
pio run -e ThinkNode_M1_ble_cli

# Upload via USB (one time)
pio run -e ThinkNode_M1_ble_cli -t upload
```

The firmware will be available at: `.pio/build/ThinkNode_M1_ble_cli/firmware.zip`

### Using BLE CLI:

1. Flash the BLE CLI firmware once via USB
2. Device advertises as `THINKNODE_M1_CLI`
3. Connect using any Bluetooth terminal app
4. Available commands:
   - `start ota` - Enter OTA update mode
   - `info` - Show device information
   - `help` - Show command list
   - `reboot` - Reboot device

## Best Practices

1. **Always download the ZIP firmware** - The nRF DFU app requires ZIP format
2. **Ensure good battery** - Low battery during update can cause failures
3. **Stay close to device** - Maintain short distance (< 5 feet) during update
4. **Don't interrupt** - Let the entire update complete without intervention
5. **Verify version** - After update, check firmware version to confirm success

## References

- [MeshCore Main Documentation](https://github.com/meshcore-dev/MeshCore)
- [MeshCore FAQ - OTA Updates](../docs/faq.md#71-q-how-to-update-nrf-rak-t114-seed-xiao-repeater-and-room-server-firmware-over-the-air-using-the-new-simpler-dfu-app)
- [Nordic nRF DFU Documentation](https://infocenter.nordicsemi.com/topic/sdk_nrf5_v17.1.0/lib_dfu_transport_ble.html)
- [Adafruit Bluefruit nRF52 Bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader)
