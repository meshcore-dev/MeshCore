# ESP32-S3-WROOM-1 with SX1276 LoRa Radio

This variant provides MeshCore firmware support for ESP32-S3-WROOM-1 boards connected to SX1276 LoRa radio modules.

## Hardware Requirements

- **MCU**: ESP32-S3-WROOM-1 module (4MB Flash, 2MB PSRAM)
- **Radio**: SX1276 LoRa transceiver (433MHz, 868MHz, or 915MHz)
- **Antenna**: Appropriate for your frequency band and impedance matched

## Pin Configuration

### Default SPI Pins (SX1276)
Edit `platformio.ini` to match your actual wiring:

```
P_LORA_NSS=10      # Chip Select (CS) - GPIO 10
P_LORA_DIO_0=9     # DIO0 interrupt (REQUIRED) - GPIO 9
P_LORA_RESET=8     # Reset pin - GPIO 8
P_LORA_SCLK=12     # SPI Clock - GPIO 12
P_LORA_MISO=13     # SPI MISO - GPIO 13
P_LORA_MOSI=11     # SPI MOSI - GPIO 11
```

**IMPORTANT**: Update these pin definitions in [platformio.ini](platformio.ini) to match your hardware connections!

### Optional Pins
Uncomment and configure in `platformio.ini` if needed:
- `PIN_VBAT_READ` - Battery voltage monitoring (ADC pin)
- `PIN_USER_BTN` - User button (default GPIO 0)
- `PIN_BOARD_SDA/SCL` - I2C for sensors (default GPIO 21/22)

## Wiring Example

```
ESP32-S3         SX1276
---------        ------
GPIO 10    -->   NSS (CS)
GPIO 9     -->   DIO0
GPIO 8     -->   RESET
GPIO 12    -->   SCK
GPIO 13    -->   MISO
GPIO 11    -->   MOSI
3.3V       -->   VCC
GND        -->   GND
```

## Building Firmware

### 1. Configure Your Pins
Edit the pin definitions in [platformio.ini](platformio.ini) to match your actual wiring.

### 2. Build Firmware
Choose the firmware type you want to build:

**Repeater:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_repeater
```

**Room Server:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_room_server
```

**USB Companion Radio:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_usb
```

**BLE Companion Radio:**
```bash
pio run -e ESP32S3_WROOM1_SX1276_companion_radio_ble
```

**Terminal Chat (for testing):**
```bash
pio run -e ESP32S3_WROOM1_SX1276_terminal_chat
```

### 3. Upload Firmware
```bash
pio run -e ESP32S3_WROOM1_SX1276_repeater -t upload
```

## Configuration Options

### Radio Settings
Edit in `platformio.ini`:
- `LORA_FREQ` - Frequency in MHz (default from main config)
- `LORA_BW` - Bandwidth in kHz (default from main config)
- `LORA_SF` - Spreading factor (default from main config)
- `LORA_TX_POWER` - TX power in dBm (2-17 for SX1276)

### Repeater Settings
- `ADVERT_NAME` - Name shown on mesh
- `ADVERT_LAT` / `ADVERT_LON` - GPS coordinates
- `ADMIN_PASSWORD` - Admin password (default: "password")
- `MAX_NEIGHBOURS` - Max neighbor nodes to track

### Room Server Settings
- `ROOM_PASSWORD` - Guest password (default: "hello")

### Companion Radio Settings
- `BLE_PIN_CODE` - Bluetooth pairing code (default: 123456)
- `BLE_NAME_PREFIX` - BLE device name prefix
- `MAX_CONTACTS` - Maximum contacts to store
- `MAX_GROUP_CHANNELS` - Maximum group channels

## Debugging

Enable debug output by uncommenting in `platformio.ini`:
```ini
-D MESH_DEBUG=1              ; General mesh debug
-D MESH_PACKET_LOGGING=1     ; Packet-level logging
-D BLE_DEBUG_LOGGING=1       ; BLE debug (companion only)
```

**Note**: DO NOT enable debug flags for companion radios in production as they interfere with serial communication.

## WiFi Companion (Optional)

To build WiFi companion firmware:
1. Uncomment the `[env:ESP32S3_WROOM1_SX1276_companion_radio_wifi]` section in `platformio.ini`
2. Set your WiFi credentials:
   - `WIFI_SSID` - Your WiFi network name
   - `WIFI_PWD` - Your WiFi password
   - `TCP_PORT` - TCP port to listen on (default: 5000)
3. Build and upload

## Troubleshooting

### Radio Not Initializing
- Check all wiring connections
- Verify pin definitions match your hardware
- Ensure 3.3V power supply is stable
- Check antenna is connected

### No LoRa Communication
- Verify frequency matches your region and other nodes
- Check antenna is appropriate for frequency band
- Ensure other nodes are using same BW/SF settings
- Try increasing TX power (max 17 dBm for SX1276)

### BLE Connection Issues
- Ensure BLE companion firmware is flashed
- Check BLE_PIN_CODE matches what you're entering
- Try resetting Bluetooth on your phone
- Make sure you're within range (BLE has limited range)

### Serial Console Issues
- Make sure baud rate is set to 115200
- For USB companion, disable MESH_DEBUG flags
- Use terminal chat firmware for command-line testing

## Advanced Configuration

### Custom SPI Pins
The ESP32-S3 allows flexible SPI pin assignment. To use different pins:
1. Edit the pin definitions in `platformio.ini`
2. Choose any valid GPIO pins
3. Avoid pins used for flash/PSRAM (GPIO 26-32, 33-37)

### Power Management
For battery-powered applications:
- Enable `PIN_VBAT_READ` for battery monitoring
- Consider deep sleep between transmissions
- Reduce TX power when possible
- Optimize advertising intervals

### I2C Sensors
To add environmental sensors:
1. Uncomment `PIN_BOARD_SDA` and `PIN_BOARD_SCL` in `platformio.ini`
2. Connect I2C sensors to those pins
3. See sensor examples in MeshCore documentation

## Supported Firmware Types

1. **Repeater** - Extends mesh network range
2. **Room Server** - BBS-style message board
3. **Companion Radio (USB)** - Serial connection to apps
4. **Companion Radio (BLE)** - Bluetooth connection to apps
5. **Companion Radio (WiFi)** - WiFi TCP connection to apps
6. **Terminal Chat** - Console-based chat for testing

## Resources

- [MeshCore Documentation](https://meshcore.co.uk/)
- [MeshCore GitHub](https://github.com/meshcore-dev/MeshCore)
- [Web Flasher](https://flasher.meshcore.co.uk/)
- [Discord Community](https://discord.gg/cYtQNYCCRK)

## License

This variant configuration is part of MeshCore and is licensed under the MIT License.
