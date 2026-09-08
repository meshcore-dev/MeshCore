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
| `Wio_Tracker_L2_standalone_lvgl` | **Standalone.** Full touch UI, Bluetooth compiled out (USB CLI only). |
| `Wio_Tracker_L2_companion_radio_usb` | Standard display companion over USB-C serial. |
| `Wio_Tracker_L2_repeater` | Standalone mesh repeater. |
| `Wio_Tracker_L2_room_server` | Standalone room/BBS server. |

## Touch UI (`ui-lvgl`)

LVGL 9 interface for the standalone build - no phone involved:

- **Chats**: direct messages and `#channels` with persisted history, delivery
  states (sent / delivered / failed with tap-to-resend), repeat-echo counter,
  quick replies, and a live flood/direct/hops route indicator per contact.
- **Contacts**: type filters, name/recency sort, detail view (last heard,
  path, distance/bearing), manual path picker (up to eight hops), zero-hop
  contact share, and path trace for repeaters. A trace is sent out along the
  chosen hops and back through the same ones, so the reply is only received
  when the first hop is within direct range.
- **Repeater admin**: saved passwords, status dashboard with auto-refresh,
  one-tap advert / clock sync / version / neighbors / reboot, full CLI.
- **Map**: offline tiles from SD with pan/zoom, own position, tappable node
  markers, day and night tile sets (sun/moon toggle).
- **Settings**: radio presets + full parameter editor with repeat mode,
  TX power, node name, timezone, brightness with auto-dim, screen timeout,
  alert tone styles, 12/24 h clock, km/mi units, node backup/restore to SD,
  factory reset.
- First-boot wizard (region / name / timezone), notification banner,
  unread badges, About screen with the node's public key.

## Offline maps

The map reads 256 px OSM raster tiles from a FAT32 card in the usual layout,
so a tile folder prepared for any other mesh device works unchanged:

```
/maps/{z}/{x}/{y}.png
```

Nothing else is required. Copy that folder to the card root and the map
renders it.

Two optional extras this variant understands:

- **Packed tiles.** `/maps/{z}/{x}.pak` holds one tile column per file:
  `'TPK1' | u32 y_min | u32 y_max | u32 offsets[n+1] | PNG blobs`
  (little-endian; equal offsets mark a missing tile). Loose tiles are read
  when no pack covers a column, so the two can be mixed. Packing matters only
  for very large sets: a few thousand pack files copy to a card in minutes,
  where the equivalent millions of loose PNGs take many hours and waste most
  of the card on cluster overhead.
- **A night tile set** in `/maps_dark/`, same layout either way. The map's
  sun/moon button swaps sets when one is present, and dims the day tiles when
  it is not.

Helper scripts live in `variants/wio-tracker-l2/tools/`: `tile_downloader.py`
(fetch or render tiles), `tile_packer.py` (pack columns) and
`tile_darkener.py` (derive a night set from the day set). None of them are
needed if you already have a tile folder.

## Build & flash

```bash
# from the MeshCore repo root
pio run -e Wio_Tracker_L2_standalone_lvgl

# bootloader mode if needed: hold User/Boot, tap RST, release - then:
pio run -e Wio_Tracker_L2_standalone_lvgl -t upload
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
- Pin map source: `meshtastic/firmware` PR #10909 and `meshtastic/device-ui`
  (`wio-l2` branches).
