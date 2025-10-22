# Compiling MeshCore Firmware from CLI

## Overview

The MeshCore project uses PlatformIO as its build system. There are two methods to compile firmware:

1. **Using the build.sh script** (recommended - handles all the formatting)
2. **Using pio commands directly** (more fine-grained control)

---

## Method 1: Using build.sh (Recommended)

The `build.sh` script automates the entire build process including firmware versioning.

### Prerequisites

```bash
# Install PlatformIO CLI
pip install platformio

# Or using apt (if available)
sudo apt install platformio
```

### Build Commands

#### Build a single firmware
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

#### Build all T-Beam Supreme companion radios (both USB and BLE)
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-matching-firmwares "T_Beam_S3_Supreme_SX1262_companion_radio"
```

#### Build all companion radios (all boards, USB and BLE)
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-companion-firmwares
```

#### Build all repeater firmwares
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-repeater-firmwares
```

#### Build all room server firmwares
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-room-server-firmwares
```

#### Build everything
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmwares
```

### Output Files

Compiled binaries are placed in the `out/` directory:
```
out/
├── T_Beam_S3_Supreme_SX1262_companion_radio_usb-v1.0.0-abc123.bin
├── T_Beam_S3_Supreme_SX1262_companion_radio_usb-v1.0.0-abc123-merged.bin
├── T_Beam_S3_Supreme_SX1262_companion_radio_ble-v1.0.0-abc123.bin
└── T_Beam_S3_Supreme_SX1262_companion_radio_ble-v1.0.0-abc123-merged.bin
```

---

## Method 2: Using PlatformIO CLI Directly

For more control or debugging, you can use `pio` commands directly.

### Basic Build
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

### Build with Custom Flags
```bash
export PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='\"v1.0.0\"' -DFIRMWARE_BUILD_DATE='\"22-Oct-2025\"'"
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

### Clean Build (remove old artifacts)
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb --target clean
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

### Verbose Output (for debugging)
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb -v
```

### Build Merged Binary (ESP32 fresh install)
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb -t mergebin
```

### List All Available Environments
```bash
pio project config | grep 'env:'
```

---

## For T-Beam Supreme Specifically

### Build USB Serial Variant
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

Output: `out/T_Beam_S3_Supreme_SX1262_companion_radio_usb-v1.0.0-<commit>.bin`

### Build BLE Variant
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_ble
```

Output: `out/T_Beam_S3_Supreme_SX1262_companion_radio_ble-v1.0.0-<commit>.bin`

### Build Both Variants
```bash
FIRMWARE_VERSION=v1.0.0 bash build.sh build-matching-firmwares "T_Beam_S3_Supreme_SX1262_companion_radio"
```

---

## Uploading Firmware to Device

### Using esptool.py (ESP32 boards like T-Beam Supreme)

```bash
# Install esptool
pip install esptool

# Flash merged binary (recommended for fresh installation)
esptool.py -p /dev/ttyACM0 -b 460800 write_flash 0x0 out/T_Beam_S3_Supreme_SX1262_companion_radio_usb-v1.0.0-*.bin-merged.bin

# Or flash individual binary
esptool.py -p /dev/ttyACM0 -b 460800 write_flash 0x10000 out/T_Beam_S3_Supreme_SX1262_companion_radio_usb-v1.0.0-*.bin
```

### Using PlatformIO (easiest)
```bash
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb --target upload -p /dev/ttyACM0
```

---

## Troubleshooting

### "pio: command not found"
Install PlatformIO:
```bash
pip install platformio
# Add to PATH if needed
export PATH=$PATH:~/.local/bin
```

### Build fails with dependency errors
```bash
# Clean build cache
pio run --target clean -e T_Beam_S3_Supreme_SX1262_companion_radio_usb

# Update libraries
pio lib update

# Rebuild
pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb
```

### "Permission denied" on /dev/ttyACM0
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Log out and back in, then:
esptool.py -p /dev/ttyACM0 -b 460800 write_flash 0x0 out/firmware.bin
```

---

## Build Automation Script

Create a helper script `build-tbeam.sh`:

```bash
#!/bin/bash
set -e

VERSION=${1:-v1.0.0}
echo "Building T-Beam Supreme Companion Firmware v$VERSION..."

# Build USB variant
echo "Building USB variant..."
FIRMWARE_VERSION=$VERSION bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_usb

# Build BLE variant
echo "Building BLE variant..."
FIRMWARE_VERSION=$VERSION bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_ble

echo "Build complete! Binaries in: out/"
ls -lh out/
```

Usage:
```bash
chmod +x build-tbeam.sh
./build-tbeam.sh v1.0.0
```

---

## GitHub Actions Integration

The build process runs automatically on tagged releases via `.github/workflows/build-companion-firmwares.yml`:

- Tag format: `companion-v1.0.0`
- Triggers automatic build of all companion firmware variants
- Artifacts uploaded to GitHub Actions
- Release draft created with firmware files

To trigger manually:
1. Go to Actions tab → Build Companion Firmwares
2. Click "Run workflow"
3. Set version and run
