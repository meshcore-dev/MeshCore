# Wiring Diagram - ESP32-S3-WROOM-1 + SX1276

## Default Configuration

```
┌─────────────────────┐         ┌──────────────┐
│   ESP32-S3-WROOM-1  │         │   SX1276     │
│                     │         │  LoRa Radio  │
├─────────────────────┤         ├──────────────┤
│                     │         │              │
│  GPIO 10 (NSS) ─────┼────────►│  NSS (CS)    │
│  GPIO  9 (DIO0)─────┼────────►│  DIO0        │
│  GPIO  8 (RESET)────┼────────►│  RESET       │
│  GPIO 12 (SCK) ─────┼────────►│  SCK         │
│  GPIO 13 (MISO)◄────┼─────────│  MISO        │
│  GPIO 11 (MOSI)─────┼────────►│  MOSI        │
│                     │         │              │
│  3.3V      ─────────┼────────►│  VCC         │
│  GND       ─────────┼────────►│  GND         │
│                     │         │              │
└─────────────────────┘         └──────────────┘
                                       │
                                       │
                                  ┌────▼────┐
                                  │ Antenna │
                                  │ (50Ω)   │
                                  └─────────┘
```

## Pin Mapping Table

| Function    | ESP32-S3 Pin | SX1276 Pin | Direction | Required |
|-------------|--------------|------------|-----------|----------|
| Chip Select | GPIO 10      | NSS        | Output    | ✅ Yes   |
| Interrupt   | GPIO 9       | DIO0       | Input     | ✅ Yes   |
| Reset       | GPIO 8       | RESET      | Output    | ✅ Yes   |
| SPI Clock   | GPIO 12      | SCK        | Output    | ✅ Yes   |
| SPI MISO    | GPIO 13      | MISO       | Input     | ✅ Yes   |
| SPI MOSI    | GPIO 11      | MOSI       | Output    | ✅ Yes   |
| DIO1        | (Not used)   | DIO1       | Input     | ⬜ No    |
| Power       | 3.3V         | VCC        | Power     | ✅ Yes   |
| Ground      | GND          | GND        | Ground    | ✅ Yes   |

## Optional Connections

### Battery Monitoring
```
Battery (+) ──[Voltage Divider]── GPIO 1 (ADC)
              (100K/47K)
```

Uncomment in platformio.ini:
```ini
-D PIN_VBAT_READ=1
```

### User Button
```
GPIO 0 ──[Button]── GND
       (with pull-up)
```

Uncomment in platformio.ini:
```ini
-D PIN_USER_BTN=0
```

### I2C Sensors
```
ESP32-S3              Sensor
--------              ------
GPIO 21 (SDA) ──────  SDA
GPIO 22 (SCL) ──────  SCL
3.3V         ──────── VCC
GND          ──────── GND
```

Uncomment in platformio.ini:
```ini
-D PIN_BOARD_SDA=21
-D PIN_BOARD_SCL=22
```

## Power Requirements

### SX1276 Module
- **Voltage**: 3.3V ±10%
- **Current (RX)**: ~10-15 mA
- **Current (TX)**: ~20-120 mA (depends on TX power)
- **Peak Current**: Up to 120 mA at max TX power

### ESP32-S3
- **Voltage**: 3.0V - 3.6V
- **Current (Active)**: 40-100 mA
- **Current (WiFi TX)**: 150-250 mA
- **Current (BLE)**: 80-100 mA

### Total System
- **Minimum supply**: 3.3V @ 300 mA recommended
- **For battery operation**: Use LDO regulator or boost converter
- **Decoupling capacitors**: 
  - 100µF near power input
  - 10µF near ESP32-S3
  - 10µF near SX1276

## Antenna Selection

### Frequency Bands
- **433 MHz**: ~17.3 cm quarter-wave
- **868 MHz**: ~8.2 cm quarter-wave  
- **915 MHz**: ~7.8 cm quarter-wave

### Antenna Types
- **Wire Antenna**: Cheap, adequate for testing
- **Helical/Spring**: Compact, moderate gain
- **1/4 Wave Whip**: Good omnidirectional performance
- **External SMA**: Best performance, allows antenna swapping

⚠️ **Always use an antenna!** Operating without an antenna can damage the SX1276 module.

## Layout Recommendations

### PCB Design Tips
1. Keep traces to SX1276 short
2. Use ground plane under RF section
3. Separate analog/RF from digital sections
4. Add test points for debugging
5. Include antenna matching circuit if using custom PCB

### Breadboard Tips
1. Use short jumper wires
2. Keep SX1276 away from switching regulators
3. Add 100nF ceramic cap close to SX1276 VCC
4. Ensure good ground connections
5. Use shielded wire for antenna connection if long

## Troubleshooting Wiring Issues

### Symptoms and Solutions

**Radio fails to initialize:**
- Check VCC is 3.3V stable
- Verify all ground connections
- Check NSS, RESET pins are connected
- Ensure MISO/MOSI not swapped

**Can receive but not transmit:**
- Check DIO0 connection
- Verify antenna is connected
- Check TX power setting not too high
- Measure VCC during TX (should stay >3.0V)

**Intermittent operation:**
- Add decoupling capacitors
- Check for loose connections
- Verify power supply can handle peak current
- Check for nearby interference sources

**Short range:**
- Check antenna impedance matching
- Verify antenna is correct frequency
- Increase TX power (within regulations)
- Check for metal objects near antenna

## Testing Procedure

### 1. Power Check
```
Multimeter: Measure VCC = 3.3V ±0.1V
            Measure GND continuity
```

### 2. SPI Communication Test
Upload firmware with `-D MESH_DEBUG=1` and check serial output for:
```
Radio initialized successfully
```

### 3. Transmission Test
Send test packet from serial console:
```
advert
```

### 4. Reception Test
Have another node nearby send packets and monitor serial output.

## Custom Pin Configuration

If you need different pins, edit `platformio.ini`:

```ini
[ESP32S3_WROOM1_SX1276]
build_flags =
  ...
  -D P_LORA_NSS=5        ; Your CS pin
  -D P_LORA_DIO_0=6      ; Your DIO0 pin
  -D P_LORA_RESET=7      ; Your RESET pin
  -D P_LORA_SCLK=18      ; Your SCK pin
  -D P_LORA_MISO=19      ; Your MISO pin
  -D P_LORA_MOSI=23      ; Your MOSI pin
```

⚠️ **Avoid these ESP32-S3 pins:**
- GPIO 26-32: Used for flash/PSRAM
- GPIO 33-37: Used for PSRAM
- GPIO 19-20: USB pins (if using USB)

## Safety Notes

1. **ESD Protection**: Handle SX1276 with anti-static precautions
2. **Antenna First**: Always connect antenna before powering on
3. **Voltage**: Never exceed 3.6V on SX1276 VCC
4. **Current**: Ensure power supply can handle TX peaks
5. **Regulations**: Operate within your region's ISM band rules

## Additional Resources

- [SX1276 Datasheet](https://www.semtech.com/products/wireless-rf/lora-core/sx1276)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [MeshCore Hardware Guide](https://github.com/meshcore-dev/MeshCore/wiki)
- [Discord #hardware channel](https://discord.gg/cYtQNYCCRK)

---

**Need help?** Join the MeshCore Discord community!
