# MeshCore Linux Variant

Native Linux support for MeshCore, targeting Raspberry Pi (Zero, 3, 4, 5) and similar SBCs with an SX1262 LoRa radio attached over SPI. Uses the [Portduino](https://github.com/meshtastic/platform-native) Arduino-compatibility layer to run the same firmware codebase on Linux without modification to the core library.

## Hardware

- Raspberry Pi (any model with SPI)
- SX1262-based LoRa module wired to the Pi's SPI bus (e.g. Waveshare SX1262 HAT, RAK2287, similar)
- SPI, IRQ, RESET, and optionally BUSY/RXEN/TXEN GPIO pins

## Build

**Dependencies** (install on the build machine and on the Pi):

```sh
# Arch Linux
sudo pacman -S libgpiod i2c-tools

# Debian/Raspberry Pi OS
sudo apt install libgpiod-dev libi2c-dev
```

**Build with PlatformIO:**

```sh
FIRMWARE_VERSION=dev pio run -e linux_repeater
# binary: .pio/build/linux_repeater/program
```

## Configuration

The binary reads `/etc/meshcored/meshcored.ini` on startup. Copy and edit the sample:

```sh
sudo mkdir -p /etc/meshcored
sudo cp variants/linux/meshcored.ini /etc/meshcored/meshcored.ini
sudo nano /etc/meshcored/meshcored.ini
```

Key settings:

| Key | Default | Notes |
|-----|---------|-------|
| `spidev` | `/dev/spidev0.0` | SPI device node |
| `lora_irq_pin` | (none) | GPIO pin number for IRQ |
| `lora_reset_pin` | (none) | GPIO pin number for RESET |
| `lora_nss_pin` | (none) | GPIO pin number for NSS/CS (if not handled by SPI driver) |
| `lora_busy_pin` | (none) | GPIO pin number for BUSY |
| `lora_freq` | `869.618` | Frequency in MHz |
| `lora_bw` | `62.5` | Bandwidth in kHz |
| `lora_sf` | `8` | Spreading factor |
| `lora_cr` | `8` | Coding rate |
| `lora_tcxo` | `1.8` | TCXO voltage (V); set to `0.0` if no TCXO |
| `lora_tx_power` | `22` | TX power in dBm |
| `advert_name` | `"Linux Repeater"` | Node name broadcast to the mesh |
| `admin_password` | `"password"` | Change this |
| `lat` / `lon` | `0.0` | GPS coordinates for advertisement |
| `data_dir` | `/var/lib/meshcore` | Where identity and prefs are stored |

## Running

Enable SPI and set GPIO permissions on the Pi:

```sh
# Raspberry Pi OS
sudo raspi-config  # Interface Options → SPI → Enable
sudo usermod -aG spi,gpio $USER
```

Run directly:

```sh
sudo .pio/build/linux_repeater/program
```

## Systemd Service

```sh
sudo cp variants/linux/meshcored.service /etc/systemd/system/
sudo useradd -r -s /sbin/nologin meshcore
sudo install -d -o meshcore -g meshcore /var/lib/meshcore
# copy binary to /usr/bin/meshcored
sudo systemctl enable --now meshcored
sudo journalctl -u meshcored -f
```

## Known Gaps / TODO

- **No CLI argument parsing** — config path is hardcoded to `/etc/meshcored/meshcored.ini`; `data_dir` is only configurable via the INI file.
- **Only repeater firmware** — there is no `linux_companion` target yet; companion radio support (BLE/serial interface to a phone app) is not implemented for Linux.
- **`formatFileSystem()`** returns `false` (not implemented) — the CLI `format` command will report failure on Linux.
- **No power management** — `board.sleep()` is a no-op; the power-saving loop in `main.cpp` never actually sleeps.
