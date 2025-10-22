#!/bin/bash
# Quick build script for T-Beam Supreme USB Companion Firmware
# Usage: bash quick-build-tbeam-usb.sh [version]

set -e

VERSION=${1:-v1.0.0}
PORT=${2:-/dev/ttyACM0}

echo "╔════════════════════════════════════════════════════════╗"
echo "║ T-Beam Supreme USB Companion Radio Build Script        ║"
echo "╚════════════════════════════════════════════════════════╝"
echo ""
echo "Version: $VERSION"
echo "Target:  T_Beam_S3_Supreme_SX1262_companion_radio_usb"
echo "Port:    $PORT"
echo ""

# Step 1: Check dependencies
echo "[1/4] Checking dependencies..."
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO not found. Installing..."
    pip install platformio
fi
echo "✓ PlatformIO found"

# Step 2: Clean previous builds
echo ""
echo "[2/4] Cleaning previous builds..."
rm -rf out
mkdir -p out
echo "✓ Clean complete"

# Step 3: Build firmware
echo ""
echo "[3/4] Building firmware (this may take 5-10 minutes)..."
FIRMWARE_VERSION=$VERSION bash build.sh build-firmware T_Beam_S3_Supreme_SX1262_companion_radio_usb
echo "✓ Build complete"

# Step 4: Show output
echo ""
echo "[4/4] Build artifacts:"
ls -lh out/
echo ""
echo "✓ All done!"
echo ""
echo "Next steps:"
echo "  1. Connect T-Beam via USB"
echo "  2. Flash with: esptool.py -p $PORT -b 460800 write_flash 0x0 out/*.bin-merged.bin"
echo "  3. Or use: pio run -e T_Beam_S3_Supreme_SX1262_companion_radio_usb --target upload -p $PORT"
echo ""
