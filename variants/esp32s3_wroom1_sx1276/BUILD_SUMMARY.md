# ESP32-S3-WROOM-1 + SX1276 Variant - Build Summary

## ✅ Successfully Created Firmware Variant

A complete MeshCore firmware build variant has been created for your **ESP32-S3-WROOM-1** board with **SX1276** LoRa radio.

## 📁 Created Files

```
variants/esp32s3_wroom1_sx1276/
├── platformio.ini       # Build configurations for all firmware types
├── target.h             # Hardware abstraction header
├── target.cpp           # Hardware initialization code
├── README.md            # Complete documentation
└── QUICKSTART.md        # Quick start guide
```

## 🔧 Available Firmware Types

Your variant includes **5 different firmware configurations**:

### 1. **Repeater** - `ESP32S3_WROOM1_SX1276_repeater`
Extends mesh network range by forwarding packets.

**Build:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_repeater
```

### 2. **Room Server** - `ESP32S3_WROOM1_SX1276_room_server`
BBS-style message board server for the mesh.

**Build:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_room_server
```

### 3. **USB Companion** - `ESP32S3_WROOM1_SX1276_companion_radio_usb`
Connect to MeshCore apps via USB serial.

**Build:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_usb
```

### 4. **BLE Companion** - `ESP32S3_WROOM1_SX1276_companion_radio_ble`
Connect to MeshCore apps via Bluetooth.

**Build:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_ble
```

### 5. **Terminal Chat** - `ESP32S3_WROOM1_SX1276_terminal_chat`
Console-based testing firmware.

**Build:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_terminal_chat
```

### 6. **WiFi Companion** (Optional, commented out)
Uncomment in `platformio.ini` and configure WiFi credentials.

## 📌 Default Pin Configuration

| Pin Function | GPIO | Description |
|-------------|------|-------------|
| P_LORA_NSS | 10 | SPI Chip Select |
| P_LORA_DIO_0 | 9 | DIO0 Interrupt (required) |
| P_LORA_RESET | 8 | Reset Pin |
| P_LORA_SCLK | 12 | SPI Clock |
| P_LORA_MISO | 13 | SPI MISO |
| P_LORA_MOSI | 11 | SPI MOSI |

**⚠️ IMPORTANT**: If your wiring is different, edit the pin definitions in `variants/esp32s3_wroom1_sx1276/platformio.ini` before building!

## 🚀 Quick Start

### 1. **Update Pin Configuration (if needed)**
```bash
nano variants/esp32s3_wroom1_sx1276/platformio.ini
```

Edit the pin definitions in the `[ESP32S3_WROOM1_SX1276]` section.

### 2. **Build Your Firmware**
```bash
cd /home/maciek/Desktop/CODE/MESH/MeshCore
pio run -e ESP32S3_WROOM1_SX1276_repeater
```

### 3. **Upload to Board**
```bash
pio run -e ESP32S3_WROOM1_SX1276_repeater -t upload
```

### 4. **Monitor Serial Output**
```bash
pio device monitor -b 115200
```

## 🔨 Build All Firmware Types

```bash
# Repeater
pio run -e ESP32S3_WROOM1_SX1276_repeater

# Room Server
pio run -e ESP32S3_WROOM1_SX1276_room_server

# USB Companion
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_usb

# BLE Companion
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_ble

# Terminal Chat
pio run -e ESP32S3_WROOM1_SX1276_terminal_chat
```

## 📝 Configuration Files

### Main Configuration
The variant is registered in the main `platformio.ini`:
```ini
[platformio]
extra_configs =
	variants/esp32s3_wroom1_sx1276/platformio.ini
	...
```

### Hardware Configuration
All hardware-specific settings are in:
- `variants/esp32s3_wroom1_sx1276/platformio.ini` - Pin definitions and build flags
- `variants/esp32s3_wroom1_sx1276/target.h` - Hardware abstraction interface
- `variants/esp32s3_wroom1_sx1276/target.cpp` - Initialization code

## 🎯 Radio Configuration

Default settings (can be changed in platformio.ini):

```ini
LORA_TX_POWER=17    # SX1276 max: 2-17 dBm
LORA_FREQ=915.0     # From main config
LORA_BW=250         # From main config
LORA_SF=11          # From main config
```

## 📚 Documentation

- **README.md** - Complete variant documentation
- **QUICKSTART.md** - Quick start guide with common issues
- **Main FAQ** - `/home/maciek/Desktop/CODE/MESH/MeshCore/docs/faq.md`

## 🔍 Troubleshooting

### Build Fails
```bash
# Clean build directory
pio run -e ESP32S3_WROOM1_SX1276_repeater -t clean

# Rebuild
pio run -e ESP32S3_WROOM1_SX1276_repeater
```

### Pin Configuration Issues
Edit: `variants/esp32s3_wroom1_sx1276/platformio.ini`
Look for the `[ESP32S3_WROOM1_SX1276]` section.

### Radio Not Working
1. Check wiring matches pin configuration
2. Verify 3.3V power supply
3. Ensure antenna is connected
4. Check frequency matches your region

## 📖 Additional Resources

- [MeshCore Website](https://meshcore.co.uk/)
- [GitHub Repository](https://github.com/meshcore-dev/MeshCore)
- [Web Flasher](https://flasher.meshcore.co.uk/)
- [Discord Community](https://discord.gg/cYtQNYCCRK)
- [CLI Reference](https://github.com/meshcore-dev/MeshCore/wiki/Repeater-&-Room-Server-CLI-Reference)

## 🎉 You're Ready!

Your ESP32-S3-WROOM-1 + SX1276 variant is fully configured and ready to build. Choose your firmware type and start building!

For the quickest start, begin with the **BLE Companion** to connect to your phone:
```bash
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_ble -t upload
```

Then download the MeshCore app and connect via Bluetooth!

---

**Note**: If you make any improvements to this variant or find issues, consider contributing back to the MeshCore project! 🚀
