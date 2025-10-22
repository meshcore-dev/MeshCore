# Quick Reference: Building T-Beam Supreme USB Firmware

## One-Liner Build

```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

**Output:** `out/T_Beam_S3_Supreme_SX1262_companion_radio_usb-v1.0.0-*.bin`

---

## Step-by-Step

### 1. Install PlatformIO (if needed)
```bash
pip install platformio
```

### 2. Build the firmware
```bash
cd /home/aquarat/Projects/MeshCore
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

### 3. Find your binary
```bash
ls -lh out/
```

You'll see:
- `*-merged.bin` ← Use this for fresh installation
- `*.bin` ← Use this if you have existing partitions

---

## Build Both USB + BLE Variants

```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-matching-firmwares "T_Beam_S3_Supreme_SX1262_companion_radio"
```

---

## Upload to Device

### Method 1: PlatformIO (simplest)
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb --target upload -p /dev/ttyACM0
```

### Method 2: esptool.py (most flexible)
```bash
pip install esptool
esptool.py -p /dev/ttyACM0 -b 460800 write_flash 0x0 out/*-merged.bin
```

---

## Alternative: Direct pio Command

If you don't want to use build.sh:

```bash
export PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='\"v1.0.0\"' -DFIRMWARE_BUILD_DATE='\"$(date '+%d-%b-%Y')\"'"
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb -t mergebin
```

---

## What build.sh Does

The `build.sh` script (inspired by GitHub Actions):

1. ✅ Sets up firmware version strings
2. ✅ Adds build date
3. ✅ Adds git commit hash
4. ✅ Runs `pio run`
5. ✅ Creates merged binary (for ESP32)
6. ✅ Copies binaries to `out/` folder
7. ✅ Names them properly: `[ENV]-[VERSION]-[HASH].[ext]`

---

## Environment Names

Available for T-Beam Supreme:

```
T_Beam_S3_Supreme_SX1262_companion_radio_usb    ← USB CDC Serial
T_Beam_S3_Supreme_SX1262_companion_radio_ble    ← Bluetooth LE
T_Beam_S3_Supreme_SX1262_repeater               ← Repeater firmware
T_Beam_S3_Supreme_SX1262_room_server            ← Room server firmware
```

List all:
```bash
pio project config | grep env:
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `pio: command not found` | `pip install platformio` |
| Build fails | `pio run --target clean -e T_Beam_S3_Supreme_SX1262_companion_radio_usb` |
| Permission denied on /dev/ttyACM0 | `sudo usermod -a -G dialout $USER` (then logout/login) |
| Need verbose output | Add `-v` flag: `pio run -e ... -v` |

---

## Full GitHub Actions Flow (Reference)

```bash
# This is what CI does:
git clone <repo>
pip install platformio
FIRMWARE_VERSION=v1.0.0 bash build.sh build-companion-firmwares
# Outputs to: out/
# Then uploads artifacts and creates release
```

---

## Automating with Custom Script

Save as `build-my-firmware.sh`:

```bash
#!/bin/bash
VERSION=${1:-v1.0.0}
FIRMWARE_VERSION=$VERSION bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_usb
echo "✓ Built: out/*"
echo "✓ Flash with: esptool.py -p /dev/ttyACM0 -b 460800 write_flash 0x0 out/*-merged.bin"
```

Then:
```bash
chmod +x build-my-firmware.sh
./build-my-firmware.sh v1.0.0
```
