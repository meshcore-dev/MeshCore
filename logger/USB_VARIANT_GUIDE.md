# Quick Reference: USB Serial Variant for T-Beam Supreme

## ✅ What Was Created

A new PlatformIO build environment for USB serial communication on the T-Beam Supreme companion radio.

## 📝 Build Environments Available

```
BLE Variant (Original):
  env: T_Beam_S3_Supreme_SX1262_companion_radio_ble
  Type: Bluetooth Low Energy with PIN 123456

USB Variant (New):
  env: T_Beam_S3_Supreme_SX1262_companion_radio_usb
  Type: USB CDC Serial Interface at 115200 baud
```

## 🔑 Key Difference

| Aspect | BLE | USB |
|--------|-----|-----|
| Interface | Bluetooth LE | USB CDC Serial |
| `-D BLE_PIN_CODE=123456` | ✅ Yes | ❌ No |
| Serial frames work | ✅ Yes | ✅ Yes |
| Needs PIN | Yes (123456) | No |

The USB variant **does NOT define BLE_PIN_CODE**, which causes `main.cpp` to use the USB CDC interface instead of BLE.

## 🚀 Build Commands

```bash
# Build USB serial variant
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb

# Build BLE variant
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_ble

# Build both
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb -e T_Beam_S3_Supreme_SX1262_companion_radio_ble
```

## 📍 Files Modified

- `/variants/lilygo_tbeam_supreme_SX1262/platformio.ini` - Added USB variant config
- `/variants/lilygo_tbeam_supreme_SX1262/USB_VARIANT_README.md` - Detailed documentation

## 💻 Testing with TypeScript Logger

After flashing the USB variant:

```bash
cd /home/aquarat/Projects/MeshCore/logger
pnpm ts-node src/index.ts
```

The app will:
1. Detect `/dev/ttyACM0` (T-Beam Supreme)
2. Send `CMD_DEVICE_QUERY`
3. Display firmware version and device info

## 🔧 How It Works

**Code flow in `examples/companion_radio/main.cpp` (ESP32 section):**

```cpp
#ifdef WIFI_SSID
  // WiFi path
#elif defined(BLE_PIN_CODE)
  // BLE path ← USB variant avoids this
#elif defined(SERIAL_RX)
  // Hardware UART path
#else
  serial_interface.begin(Serial);  // ← USB variant takes this
#endif
```

Since the USB variant doesn't define `BLE_PIN_CODE`, it skips BLE and uses USB CDC.

## ✨ Both Variants Coexist

- Original BLE companion radio is **unchanged**
- New USB variant is **an additional option**
- Both can be built independently
- Same firmware version number
- Same feature set

## 📦 Dependencies

No new dependencies required - uses existing `ArduinoSerialInterface` class for USB CDC communication.
