# WT32-ETH01 + Heltec HT-RA62

MeshCore companion firmware for the original WT32-ETH01 (ESP32, LAN8720, 4 MB flash),
with an HT-RA62 SX1262 LoRa module. Not suitable for the WT32-ETH01-EVO.
The default settings in this repository use 869.618 MHz and require the **HT-RA62-HF**
(863–928 MHz); the LF version requires different radio settings.

## Wiring

GPIO numbers are not the physical pin numbers on the WT32 connector.

| HT-RA62 module pin | WT32-ETH01 |
| --- | --- |
| 3 — 3V3 | 3V3 |
| 2, 9, 16 — GND | Common GND |
| 12 — SCK | GPIO14 / IO14 |
| 14 — MOSI | GPIO32 / CFG |
| 13 — MISO | GPIO35 / IO35 |
| 15 — NSS | GPIO33 / 485_EN |
| 4 — RST | GPIO4 / IO4 |
| 10 — BUSY | GPIO36 / IO36 |
| 6 — DIO1 | GPIO39 / IO39 |
| 5 — TXEN, 7 — DIO2, 8 — DIO3, 11 — RXEN | Leave externally unconnected; see below |
| 1 — ANT or IPEX connector | Antenna for the module's frequency band |

The standard Heltec schematic already connects DIO2 to the RF control through R6 and
DIO3 to the oscillator power supply. The firmware enables RF control
on DIO2 and configures DIO3 to 1.8 V; verify this voltage if your module
has a different oscillator or hardware revision. Do not connect TXEN to an ESP32 output
in parallel with DIO2. No external jumpers are required on the standard module.

GPIO35/36/39 are used only as inputs. This mapping leaves the boot strapping pins
and UART0 available, and does not use the GPIOs reserved for the LAN8720
(0, 16, 18, 19, 21, 22, 23, 25, 26, 27). I2C is disabled because its
default pins, 32/33, are assigned to the radio. No display,
GPS, or battery measurement is configured.

Power the WT32 from a stable 5 V supply and the LoRa module at
**3.3 V**, with a common ground; never apply 5 V to the signal pins or the HT-RA62 module.
Size the power supply for both boards (at least 500 mA for the
WT32 alone according to the manufacturer, with additional capacity for the radio).
Use short SPI wires and decoupling capacitors close to the module (100 nF and 10 µF).
Connect the antenna before transmitting.

## Building

From the repository root, with PlatformIO Core installed:

```sh
pio run -e WT32_ETH01_HT_RA62_companion_radio_ethernet -t mergebin
pio run -e WT32_ETH01_HT_RA62_companion_radio_ble -t mergebin
pio run -e WT32_ETH01_HT_RA62_companion_radio_usb -t mergebin
```

If `pio` is not in your PATH, it is available on this machine at
`/home/fabio/.platformio/penv/bin/pio`.
Select one of the three versions; each command generates
`.pio/build/<environment name>/firmware.bin` and `firmware-merged.bin`.
The merged file includes the bootloader and partition table and is flashed at offset
`0x0`; `firmware.bin` contains only the application and is flashed at `0x10000`.
The BLE environment uses `min_spiffs.csv` to leave room for the Bluetooth stack.
Switching between BLE and the other environments changes the partition table: back up
your identity and data first, and use the full image when switching.

## Flashing

The WT32-ETH01 has no built-in USB interface. A USB-to-UART adapter with
**3.3 V logic levels** is required:

| USB-UART | WT32 programming connector |
| --- | --- |
| TX | RX0 / GPIO3 |
| RX | TX0 / GPIO1 |
| GND | GND |

Do not confuse TX0/RX0 with TXD/RXD (GPIO17/5).
Power the board separately if the adapter cannot supply enough current.
Connect GPIO0 to GND and reset or power on the board to enter the bootloader:

```sh
pio run -e WT32_ETH01_HT_RA62_companion_radio_ethernet -t upload --upload-port /dev/ttyUSB0
```

Remove the GPIO0–GND connection and reset the board after flashing.
Replace the environment and serial port as needed.

## Connection and hardware checks

- **Ethernet:** connect the cable to a network with DHCP. The serial monitor at
  115200 baud shows the IP address and TCP port **5000** after radio initialization.
  Use a client compatible with the MeshCore companion TCP protocol;
  this is not an HTTP server. No Wi-Fi SSID or password is required. The transport
  does not add authentication or TLS: use it on a trusted LAN.
- **BLE:** pair the companion through the app; the initial PIN is **123456**.
- **USB:** the companion protocol runs over the UART0 adapter at 115200 baud.
  Close the serial monitor before opening the client.

The initial preset uses 869.618 MHz, BW 62.5 kHz, SF8, and 8 dBm transmit power.
Set your network's parameters through the client; saved settings
may override the build-time defaults.

Building the firmware does not verify the physical circuit. On first boot, check
that `ERROR: radio init failed` does not appear, then test transmission and reception with a
known working node. For Ethernet, also test disconnecting and reconnecting the cable
and reconnecting the TCP client. If radio initialization fails, the companion
stops before starting the network interface.

## Hardware references

- [Heltec HT-RA62 datasheet](https://resource.heltec.cn/download/HT-RA62/HT-RA62%28Rev1.1%29.pdf)
- [Heltec module schematic](https://resource.heltec.cn/download/HT-RA62/HT-RA62_Schematic_diagram.pdf)
- [Heltec reference design](https://resource.heltec.cn/download/HT-RA62/HT-RA62_Reference_design.pdf)
- [Wireless-Tag WT32-ETH01 datasheet](https://www.tme.eu/Document/1503f6f34f86647f395694ec5f8ce17b/WT32-ETH01_datasheet_V1.3-en.pdf)
- The PHY configuration comes from the Arduino core's `wt32-eth01` variant:
  LAN8720 address 1, power/clock-enable GPIO16, MDC GPIO23, MDIO GPIO18,
  RMII clock input on GPIO0.
