# MeshCore Linux Variant

Native Linux support for MeshCore, targeting Raspberry Pi (Zero, 3, 4, 5) and similar SBCs with an SX1262 LoRa radio attached over SPI. Uses the [Portduino](https://github.com/meshtastic/platform-native) Arduino-compatibility layer to run the same firmware codebase on Linux without modification to the core library.

## Hardware

- Raspberry Pi (any model with SPI)
- SX1262-based LoRa module wired to the Pi's SPI bus (e.g. Waveshare SX1262 HAT, PoW SX1262 HAT)
- SPI, IRQ, RESET, and optionally BUSY/RXEN/TXEN GPIO pins

## Build

**Dependencies** (install on the build machine and on the Pi):

```sh
# Arch Linux
sudo pacman -S libgpiod i2c-tools

# Debian/Raspberry Pi OS
sudo apt install libgpiod-dev libi2c-dev
```

**Build with `build.sh`** (recommended — embeds version and commit hash):

```sh
FIRMWARE_VERSION=dev ./build.sh build-firmware linux_repeater
# binary: .pio/build/linux_repeater/program
```

Alternatively, build directly with PlatformIO (no version metadata):

```sh
FIRMWARE_VERSION=dev pio run -e linux_repeater
```

## Setup

### 1. Install the binary

```sh
sudo cp .pio/build/linux_repeater/program /usr/bin/meshcored
```

### 2. Create the config file

Two ready-made templates are provided in `variants/linux/`:

| Template | Hardware |
|----------|----------|
| `meshcored.ini.pow-sx1262` | RPi Zero 2W + PoW SX1262 HAT |
| `meshcored.ini.waveshare` | RPi 3/4/5 + Waveshare SX1262 LoRa HAT |

```sh
sudo mkdir -p /etc/meshcored
# Pick the template that matches your hardware:
sudo cp variants/linux/meshcored.ini.waveshare /etc/meshcored/meshcored.ini
sudo nano /etc/meshcored/meshcored.ini
```

The config file has two roles:

- **Hardware config** (always read on every startup): SPI device, GPIO pin numbers, LoRa radio parameters.
- **First-run node defaults**: `advert_name`, `admin_password`, `lat`, `lon`. On the first boot these are saved to `data_dir`. After that, use the serial CLI to change them (`set name`, `set password`, etc.) — the INI values are no longer consulted for these fields.

Key settings:

| Key | Default | Notes |
|-----|---------|-------|
| `spidev` | `/dev/spidev0.0` | SPI device node |
| `lora_irq_pin` | (none) | GPIO line number for IRQ |
| `lora_reset_pin` | (none) | GPIO line number for RESET |
| `lora_nss_pin` | (none) | GPIO line number for NSS/CS (if not handled by the SPI driver) |
| `lora_busy_pin` | (none) | GPIO line number for BUSY |
| `lora_freq` | `869.618` | Frequency in MHz |
| `lora_bw` | `62.5` | Bandwidth in kHz |
| `lora_sf` | `8` | Spreading factor |
| `lora_cr` | `8` | Coding rate |
| `lora_tcxo` | `1.8` | TCXO voltage (V); set to `0.0` if your module has no TCXO |
| `lora_tx_power` | `22` | TX power in dBm |
| `advert_name` | `"Linux Repeater"` | Node name — first-run default only |
| `admin_password` | `"password"` | Admin password — **change this**, first-run default only |
| `lat` / `lon` | `0.0` | GPS coordinates for advertisement — first-run default only |
| `data_dir` | `/var/lib/meshcore` | Where identity and node prefs are persisted |

### 3. Enable SPI and GPIO access

```sh
# Raspberry Pi OS
sudo raspi-config          # Interface Options → SPI → Enable
sudo usermod -aG spi,gpio $USER
```

On Arch Linux and other distributions without `spi`/`gpio` groups, use the provided udev rules instead (also works on Raspberry Pi OS):

```sh
sudo cp variants/linux/99-meshcore.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### 4. Run

**Directly** (for testing):

```sh
sudo /usr/bin/meshcored
```

`sudo` is needed on first run to create `data_dir` if it doesn't exist. Once the directory is created and owned appropriately, it can run as a non-root user.

**As a systemd service** (recommended for production):

```sh
sudo cp variants/linux/meshcored.service /etc/systemd/system/
sudo cp variants/linux/99-meshcore.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo useradd -r -s /sbin/nologin meshcore
sudo mkdir -p /var/lib/meshcore
sudo chown meshcore:meshcore /var/lib/meshcore
sudo chmod 640 /etc/meshcored/meshcored.ini
sudo chown root:meshcore /etc/meshcored/meshcored.ini
sudo systemctl daemon-reload
sudo systemctl enable --now meshcored
sudo journalctl -u meshcored -f
```

### 5. Reconfiguring after first run

Node name, password, and location can be changed via the serial CLI after first boot:

```
set name <name>
set password <password>
set lat <lat>
set lon <lon>
```

To reset all node prefs and re-apply the INI file defaults, delete the saved prefs and restart:

```sh
sudo rm /var/lib/meshcore/com_prefs
sudo systemctl restart meshcored
```

> **Note:** LoRa radio parameters (`lora_freq`, `lora_bw`, `lora_sf`, `lora_cr`, `lora_tx_power`) are also first-run defaults. After first boot they are saved in `com_prefs` and the INI values are no longer read for those fields. To apply a changed radio parameter, use the CLI (`set freq`, `set sf`, etc.) or reset prefs as above.

## Known Gaps / TODO

- **No CLI argument parsing** — config path is hardcoded to `/etc/meshcored/meshcored.ini`; `data_dir` is only configurable via the INI file.
- **Only repeater firmware** — there is no `linux_companion` target yet; companion radio support (BLE/serial interface to a phone app) is not implemented for Linux.
- **`formatFileSystem()`** returns `false` (not implemented) — the CLI `format` command will report failure on Linux.
- **No power management** — `board.sleep()` is a no-op; the power-saving loop in `main.cpp` never actually sleeps.
- **Portduino branding** — on startup the binary identifies itself as "An application written with portduino" with a Meshtastic bug URL. This is hardcoded in the Portduino framework and cannot be changed without patching the framework.
