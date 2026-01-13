# Quick Start Guide - ESP32-S3-WROOM-1 with SX1276

## Hardware Setup

### Required Components
- ESP32-S3-WROOM-1 development board
- SX1276 LoRa module (433/868/915 MHz)
- Appropriate antenna for your frequency
- USB-C cable for programming

### Wiring Diagram

Connect your SX1276 module to the ESP32-S3 as follows:

| ESP32-S3 Pin | SX1276 Pin | Function |
|--------------|------------|----------|
| GPIO 10      | NSS        | Chip Select |
| GPIO 9       | DIO0       | Interrupt |
| GPIO 8       | RESET      | Reset |
| GPIO 12      | SCK        | SPI Clock |
| GPIO 13      | MISO       | SPI MISO |
| GPIO 11      | MOSI       | SPI MOSI |
| 3.3V         | VCC        | Power |
| GND          | GND        | Ground |

**⚠️ IMPORTANT**: These are the **default** pin assignments. If your wiring is different, you MUST update the pin definitions in `platformio.ini` before building.

## Customizing Pin Configuration

1. Open `variants/esp32s3_wroom1_sx1276/platformio.ini`
2. Find the `[ESP32S3_WROOM1_SX1276]` section
3. Update the pin definitions to match your wiring:

```ini
-D P_LORA_NSS=10       ; Your CS pin
-D P_LORA_DIO_0=9      ; Your DIO0 pin
-D P_LORA_RESET=8      ; Your RESET pin
-D P_LORA_SCLK=12      ; Your SPI SCK pin
-D P_LORA_MISO=13      ; Your SPI MISO pin
-D P_LORA_MOSI=11      ; Your SPI MOSI pin
```

## Building Firmware

### Step 1: Choose Firmware Type

**For Mesh Network Extension (Repeater):**
```bash
pio run -e ESP32S3_WROOM1_SX1276_repeater
```

**For Message Board Server (Room Server):**
```bash
pio run -e ESP32S3_WROOM1_SX1276_room_server
```

**For Phone/Computer Connection via USB:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_usb
```

**For Phone/Computer Connection via Bluetooth:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_ble
```

### Step 2: Upload to Board

```bash
pio run -e ESP32S3_WROOM1_SX1276_repeater -t upload
```

Replace `repeater` with your chosen firmware type.

### Step 3: Monitor Serial Output

```bash
pio device monitor -b 115200
```

## Initial Configuration

### For Repeater/Room Server

After flashing, connect via serial console (115200 baud) and set:

1. **Set your region's frequency:**
```
set freq 915.0
```
(Use 868.0 for EU, 915.0 for US, 433.0 for Asia/Africa - check your local regulations)

2. **Set your location (optional but recommended):**
```
set lat 40.7128
set long -74.0060
```

3. **Set a custom name:**
```
set name "My Repeater"
```

4. **Change admin password:**
```
password mynewpassword
```

5. **For Room Server, set guest password:**
```
set guest.password myguestpass
```

### For Companion Radio (BLE)

1. Flash the BLE companion firmware
2. Download the MeshCore app on your phone:
   - Android: [APK Download](https://files.liamcottle.net/MeshCore)
   - iOS: App Store
3. Open the app and connect via Bluetooth
4. Default pairing code: `123456`

### For Companion Radio (USB)

1. Flash the USB companion firmware
2. Open web client: https://client.meshcore.co.uk/
3. Connect via USB Serial

## Regional Settings

Configure your radio for your region by editing `platformio.ini`:

**United States (915 MHz):**
```ini
-D LORA_FREQ=915.0
-D LORA_BW=250
-D LORA_SF=11
-D LORA_TX_POWER=17
```

**Europe (868 MHz):**
```ini
-D LORA_FREQ=868.0
-D LORA_BW=250
-D LORA_SF=11
-D LORA_TX_POWER=14
```

**Asia/Africa (433 MHz):**
```ini
-D LORA_FREQ=433.0
-D LORA_BW=250
-D LORA_SF=11
-D LORA_TX_POWER=17
```

## Testing Your Setup

### 1. Check Radio Initialization
Monitor serial output on boot. You should see:
```
MeshCore starting...
Radio initialized successfully
```

### 2. Send Test Advertisement
For repeater/room server via serial console:
```
advert
```

### 3. Check Radio Stats
```
stats
```

## Common Issues

### "Radio initialization failed"
- Double-check all wiring connections
- Verify pin definitions in platformio.ini match your wiring
- Check 3.3V power supply is stable
- Ensure antenna is connected

### "No devices found" when scanning
- Verify frequency matches other devices
- Check BW and SF settings match
- Ensure you're within range
- Try increasing TX power

### BLE won't pair
- Ensure BLE firmware is flashed (not USB version)
- Check pairing code is 123456
- Reset Bluetooth on your phone
- Move closer to the device

### Can't connect via USB
- Use correct baud rate (115200)
- Try different USB cable/port
- Check USB drivers are installed
- For Linux: check user is in dialout group

## Next Steps

1. **Join the Community**: [Discord](https://discord.gg/cYtQNYCCRK)
2. **Read the Docs**: [MeshCore Wiki](https://github.com/meshcore-dev/MeshCore/wiki)
3. **Explore Features**: Try different firmware types
4. **Deploy Network**: Set up repeaters for wider coverage

## Need Help?

- Check the [FAQ](https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md)
- Ask on [Discord](https://discord.gg/cYtQNYCCRK)
- Review [MeshCore Documentation](https://meshcore.co.uk/)

Happy Meshing! 📡
