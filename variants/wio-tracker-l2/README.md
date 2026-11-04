# MeshCore for Seeed Wio Tracker L2 Pro

MeshCore port for the **Seeed Studio Wio Tracker L2 Pro** (pre-release hardware;
pin map may change on production units).

## Hardware

| Component | Detail |
|-----------|--------|
| MCU module | Wio-S3: ESP32-S3, 16 MB flash (QIO), 8 MB PSRAM (OPI) |
| LoRa | SX1262, 862-930 MHz (SCK 4, MISO 5, MOSI 6, CS 21, RST 7, BUSY 8, DIO1 9, DIO2 = RF switch, TCXO 1.8 V) |
| GNSS | Quectel L76K (GPS/BeiDou/GLONASS/QZSS), UART: module TX to GPIO 18, module RX from GPIO 17, 9600 baud |
| Display | 3.2" 320x240 NV3031B on quad-SPI (SCLK 42, IO0-3 = 41/40/39/38, CS 46), 75 MHz |
| Touch | GT911 capacitive, I2C 0x5D |
| Backlight | LP5814 LED driver, I2C 0x2C |
| I/O expander | TCA9535, I2C 0x21 - gates power/reset for GNSS, LCD, touch, SD, speaker amp, battery ADC, Grove |
| Battery ADC | ADS1115, I2C 0x48, AIN0 through x2 divider |
| USB-C detect | AW35615 CC controller, I2C 0x22 (charge detection) |
| Audio | ES8311 codec (speaker, alert tones) + ES7243E mic ADC (unused) on I2S |
| I2C bus | SDA 47, SCL 48 |
| Buttons | User/Boot = GPIO 0; Wake/lock button on expander P00 |
| SD card | SDIO 1-bit: CLK 2, CMD 3, D0 1 - offline map tiles, logs |

## Firmware flavors

| Env | What it does |
|-----|--------------|
| `Wio_Tracker_L2_companion_radio_ble` | **Phone companion.** Standard display UI, MeshCore app over BLE (pairing PIN `123456`), on-screen Bluetooth toggle page. |
| `Wio_Tracker_L2_companion_radio_usb` | Standard display companion over USB-C serial. |
| `Wio_Tracker_L2_repeater` | Standalone mesh repeater. |
| `Wio_Tracker_L2_room_server` | Standalone room/BBS server. |

## Build & flash

```bash
# from the MeshCore repo root
pio run -e Wio_Tracker_L2_companion_radio_ble

# bootloader mode if needed: hold User/Boot, tap RST, release - then:
pio run -e Wio_Tracker_L2_companion_radio_ble -t upload
```

The board enumerates as a native USB-CDC port (auto-reset via 1200-bps touch
is enabled). For the BLE companion build, pair from the MeshCore app with the
PIN shown on the device screen.

## Port notes

- **Everything hangs off the TCA9535 expander.** `WioTrackerL2Board::begin()`
  replays Seeed's power-up sequence; order and delays matter (especially the
  500 ms LCD reset settle). If the expander probe fails, the firmware still
  runs but GPS/display/battery reads are dead.
- **Battery** is read via the ADS1115 at +/-4.096 V FSR and mapped through a
  LiPo discharge curve.
- **GNSS**: GPS+BeiDou+GLONASS are enabled at start (`$PCAS04,7`). A watchdog
  resets the module if it tracks 4+ strong satellites for minutes without
  producing a fix. Raw NMEA can be logged to SD for diagnosis.
- **Power**: CPU drops to 80 MHz while the screen sleeps; the speaker amp and
  I2S clocks run only while a tone plays; the GNSS and Grove rails are gated;
  idle Bluetooth switches to slow advertising.
- **Display orientation**: `offset_rotation=1` (landscape). If a unit shows
  the UI upside-down, use 3 in `WioTrackerL2Display.h`.
- Pin map source: `meshtastic/firmware` [PR #10909](https://github.com/meshtastic/firmware/pull/10909),
  since merged as [`variants/esp32s3/seeed_wio_tracker_L2`](https://github.com/meshtastic/firmware/tree/master/variants/esp32s3/seeed_wio_tracker_L2),
  and the `meshtastic/device-ui` [`wio-l2` branch](https://github.com/meshtastic/device-ui/tree/wio-l2).
