# T-Beam Supreme USB Serial Companion Radio Variant

## What Was Added

A new PlatformIO build environment has been created for the T-Beam Supreme that supports USB serial communication via the companion radio firmware.

### File Modified
`/home/aquarat/Projects/MeshCore/variants/lilygo_tbeam_supreme_SX1262/platformio.ini`

## Variant Details

### BLE Variant (Existing)
```
[env:T_Beam_S3_Supreme_SX1262_companion_radio_ble]
- Uses: Bluetooth Low Energy (BLE)
- PIN Code: 123456
- Configuration: -D BLE_PIN_CODE=123456
```

### USB Variant (New)
```
[env:T_Beam_S3_Supreme_SX1262_companion_radio_usb]
- Uses: USB CDC Serial Interface
- No BLE PIN CODE defined (crucial difference)
- Configuration: No -D BLE_PIN_CODE flag
```

## Key Difference

The USB variant **omits** the `-D BLE_PIN_CODE=123456` flag. This is critical because in `main.cpp`, the serial interface selection uses conditional compilation:

```cpp
#ifdef WIFI_SSID
  // WiFi
#elif defined(BLE_PIN_CODE)
  // BLE Interface <- Takes this path if BLE_PIN_CODE is defined
#elif defined(SERIAL_RX)
  // Hardware Serial
#else
  serial_interface.begin(Serial);  // USB CDC <- Takes this path for USB variant
#endif
```

By removing `BLE_PIN_CODE`, the code falls through to the `#else` clause, which initializes the USB CDC interface.

## How to Build

### Build the USB variant
```bash
cd /home/aquarat/Projects/MeshCore
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

### Build the BLE variant (unchanged)
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_ble
```

### Build both
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb -e T_Beam_S3_Supreme_SX1262_companion_radio_ble
```

## Using the USB Variant

Once flashed with the USB variant, you can:

1. Connect via USB CDC at 115200 baud
2. Use your TypeScript logger app:
   ```bash
   cd /home/aquarat/Projects/MeshCore/logger
   pnpm ts-node src/index.ts
   ```

3. Send serial frames using the binary protocol:
   - Frame format: `<` + Length LSB + Length MSB + Payload
   - Commands work as before (e.g., `CMD_DEVICE_QUERY`)

## Comparison Table

| Feature | BLE Variant | USB Variant |
|---------|-------------|------------|
| Interface | Bluetooth Low Energy | USB CDC Serial |
| PIN Required | Yes (123456) | No |
| BLE_PIN_CODE flag | ✅ Defined | ❌ Not defined |
| Connection method | BLE app | minicom, pyserial, TypeScript logger |
| Battery impact | Lower | Slightly higher (USB always on) |
| Range | ~50m | USB cable only |
| Display UI | Included | Included |

## Notes

- Both variants include the full companion radio UI and feature set
- The USB variant still supports the same command protocol as BLE
- All other build flags and dependencies remain identical
- This follows the same pattern used by other boards in the repository (Xiao S3, Heltec T114, etc.)
