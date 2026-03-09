# Thinknode M1 BLE CLI Firmware - Quick Start Guide

## Overview

The **BLE CLI firmware** is a lightweight firmware variant for the Thinknode M1 that provides wireless command-line access via Bluetooth. This is the easiest way to trigger OTA firmware updates without needing a USB cable.

## When to Use BLE CLI Firmware

Use this firmware if you:
- Want to update firmware wirelessly via Bluetooth OTA
- Need CLI access without USB cables
- Frequently switch between firmware variants
- Want a simple way to check device status remotely

## Installation

### Option 1: Flash via USB (One Time Setup)

```bash
# Using PlatformIO
cd MeshCore
pio run -e ThinkNode_M1_ble_cli -t upload
```

### Option 2: Via Web Flasher

1. Visit [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk)
2. Select Thinknode M1
3. Choose "BLE CLI" firmware variant
4. Click Flash

## Usage

### 1. Connect via Bluetooth

The device advertises as: **`THINKNODE_M1_CLI`**

**Android apps:**
- Serial Bluetooth Terminal
- BlueTerm
- nRF UART (in nRF Connect)

**iOS apps:**
- nRF UART
- BlueTerm
- LightBlue (use UART service)

### 2. Available Commands

Once connected, you'll see a prompt: `>`

#### `start ota`
Enters OTA firmware update mode. Device will restart and advertise as `THINKNODE_M1_OTA`.

**Example:**
```
> start ota
Entering OTA mode...
Device will now advertise as: THINKNODE_M1_OTA
Use nRF DFU app to upload firmware.
```

After this command:
1. Device reboots in DFU mode
2. Open nRF DFU app
3. Select `THINKNODE_M1_OTA`
4. Choose firmware ZIP file
5. Upload completes
6. Device reboots with new firmware

#### `info`
Shows device information including battery voltage, MAC address, and board details.

**Example:**
```
> info
Device: Elecrow ThinkNode-M1
Board: ThinkNode M1
MCU: nRF52840
Firmware: BLE CLI Mode
Battery: 3872 mV
BLE MAC: AA:BB:CC:DD:EE:FF
```

#### `help`
Displays list of available commands.

#### `reboot`
Reboots the device immediately.

## Complete OTA Update Workflow

### Step 1: Flash BLE CLI (One Time)
```bash
pio run -e ThinkNode_M1_ble_cli -t upload
```

### Step 2: Prepare Firmware ZIP
Download desired firmware ZIP from [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk):
- Companion Radio (BLE)
- Companion Radio (USB)
- Repeater
- Room Server
- Or build your own: `pio run -e ThinkNode_M1_repeater`

Transfer ZIP to your smartphone.

### Step 3: Enter OTA Mode Wirelessly
1. Connect to `THINKNODE_M1_CLI` via Bluetooth terminal
2. Type: `start ota`
3. Press Enter
4. Device reboots in DFU mode

### Step 4: Flash Firmware via Bluetooth
1. Open **nRF Device Firmware Update** app
2. Tap **Settings** → Enable **Packets receipt notifications** → Set to **8**
3. Tap **Select Device** → Choose **THINKNODE_M1_OTA**
4. Tap **Select File** → Choose firmware ZIP
5. Tap **Upload** → Wait 2-5 minutes
6. Device reboots with new firmware

### Step 5: Update Again? (Optional)
To update firmware again:
- Flash BLE CLI firmware via OTA (from step 4)
- Repeat steps 3-4

## Comparison with Other Methods

| Method | Wireless CLI | Wireless OTA | USB Required |
|--------|--------------|--------------|--------------|
| **BLE CLI Firmware** | ✅ Yes | ✅ Yes | Only once |
| Companion BLE | ❌ No | ✅ Yes | Every time for CLI |
| Companion USB | ❌ No | ✅ Yes | Every time for CLI |
| Repeater/Room Server | ✅ Yes (via app) | ✅ Yes | No |

## Tips

1. **Keep BLE CLI as backup**: Flash BLE CLI once, then switch to your main firmware. You can always OTA back to BLE CLI to trigger another update.

2. **Battery level**: Check battery with `info` command before OTA updates. Keep above 30%.

3. **Range**: Stay within 5 feet of device during OTA update.

4. **Serial debugging**: BLE CLI also works via USB serial at 115200 baud for debugging.

5. **Quick switching**: BLE CLI → OTA mode → Flash new firmware → BLE CLI → OTA mode... (cycle for testing)

## Troubleshooting

### Can't see THINKNODE_M1_CLI
- Make sure BLE CLI firmware is flashed
- Device might be in power-save mode - press reset button
- Check Bluetooth is enabled on phone
- Try refreshing scan in terminal app

### Device not entering OTA mode
- Check serial console for error messages
- Make sure battery is charged
- Try `reboot` command first, then `start ota`

### OTA update fails
- Ensure using ZIP firmware file (not .bin)
- Try reducing packet count to 6 in nRF DFU settings
- Stay close to device during update
- Check battery level

## Source Code

The BLE CLI firmware source is available at:
- Main: `variants/thinknode_m1/ble_cli_main.cpp`
- Board: `variants/thinknode_m1/ThinkNodeM1Board.cpp`
- Build config: `variants/thinknode_m1/platformio.ini`

## References

- [Full Bluetooth OTA Guide](../../docs/thinknode_m1_bluetooth_ota.md)
- [Thinknode M1 README](README.md)
- [MeshCore Documentation](../../docs/docs.md)
