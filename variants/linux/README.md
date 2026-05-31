# MeshCore Linux Variant

Native Linux support for MeshCore, targeting Raspberry Pi (Zero, 3, 4, 5) and similar SBCs with an SX1262 LoRa radio attached over SPI. Uses [ArduLinux — Arduino API for Linux](https://github.com/l5yth/ardulinux) to run the same firmware codebase on Linux without modification to the core library.

## Hardware

- Raspberry Pi (any model with SPI)
- SX1262-based LoRa module wired to the Pi's SPI bus (e.g. Waveshare SX1262 HAT, PoW SX1262 HAT)
- SPI, IRQ, RESET, and optionally BUSY/RXEN/TXEN GPIO pins

## Build

**Dependencies** (install on the build machine and on the Pi):

```sh
# Arch Linux
sudo pacman -S libgpiod i2c-tools bluez-libs libuv

# Debian/Raspberry Pi OS
sudo apt install libgpiod-dev libi2c-dev libbluetooth-dev libuv1-dev
```

The ArduLinux platform always links `bluetooth`, `uv`, `pthread`, and
`stdc++fs`; `gpiod`/`i2c` are added automatically when libgpiod is detected via
`pkg-config` (without it the build falls back to simulated GPIO/I2C). Missing
`bluez-libs`/`libbluetooth-dev` shows up as a `cannot find -lbluetooth` link
error.

You also need **PlatformIO Core** (`pio`) to build:

```sh
# Arch Linux
sudo pacman -S platformio        # or: pipx install platformio

# Debian/Raspberry Pi OS
pipx install platformio          # or: pip install --user platformio
```

**Build with `build.sh`** (recommended — embeds version and commit hash):

```sh
FIRMWARE_VERSION=dev ./build.sh build-firmware linux_repeater
# binary: .pio/build/linux_repeater/meshcored
```

Alternatively, build directly with PlatformIO (no version metadata):

```sh
FIRMWARE_VERSION=dev pio run -e linux_repeater
```

## Setup

### 1. Install the binary

```sh
sudo install -m 755 .pio/build/linux_repeater/meshcored /usr/bin/meshcored
```

### 2. Create the config file

Two ready-made templates are provided in `variants/linux/`:

| Template | Hardware |
|----------|----------|
| `meshcored.ini.pow-sx1262` | RPi Zero 2W + PoW SX1262 HAT |
| `meshcored.ini.waveshare` | RPi 3/4/5 + Waveshare SX1262 LoRa HAT |

```sh
# Pick the template that matches your hardware (install -D creates /etc/meshcored):
sudo install -D -m 644 variants/linux/meshcored.ini.waveshare /etc/meshcored/meshcored.ini
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
| `lora_rxen_pin` | (none) | GPIO line number for RX enable (RF switch); omit if unused |
| `lora_txen_pin` | (none) | GPIO line number for TX enable (RF switch); omit if unused |
| `lora_freq` | `869.618` | Frequency in MHz |
| `lora_bw` | `62.5` | Bandwidth in kHz |
| `lora_sf` | `8` | Spreading factor |
| `lora_cr` | `8` | Coding rate |
| `lora_tcxo` | `1.8` | TCXO voltage (V); set to `0.0` if your module has no TCXO |
| `lora_tx_power` | `22` | TX power in dBm |
| `current_limit` | `140` | Radio over-current protection limit in mA |
| `dio2_as_rf_switch` | `0` | Set to `1` to use DIO2 as the RF switch control (depends on module wiring) |
| `rx_boosted_gain` | `1` | `1` enables the SX126x RX boosted-gain mode; `0` disables |
| `advert_name` | `"Linux Repeater"` | Node name — first-run default only |
| `admin_password` | `"password"` | Admin password — **change this**, first-run default only |
| `lat` / `lon` | `0.0` | GPS coordinates for advertisement — first-run default only |
| `data_dir` | `/var/lib/meshcore` | Where identity and node prefs are persisted |

### 3. Enable SPI and GPIO access

First make sure the SPI interface is actually enabled — the radio needs a
`/dev/spidev*` node. Check with `ls /dev/spidev*`; if there is none:

```sh
# Raspberry Pi OS
sudo raspi-config          # Interface Options → SPI → Enable, then reboot

# Arch Linux ARM (no raspi-config): enable the SPI device-tree overlay
echo 'dtparam=spi=on' | sudo tee -a /boot/config.txt   # then reboot
```

> The boot config path varies by image — it is `/boot/config.txt` on most
> Raspberry Pi images but `/boot/firmware/config.txt` on some. After rebooting,
> confirm `/dev/spidev0.0` exists.

Then grant access to the SPI and GPIO devices. On Raspberry Pi OS you can use the
`spi`/`gpio` groups:

```sh
sudo usermod -aG spi,gpio $USER
```

On Arch Linux and other distributions without `spi`/`gpio` groups, use the provided udev rules instead (also works on Raspberry Pi OS); these grant access to the `meshcore` group used by the systemd service:

```sh
sudo install -m 644 variants/linux/99-meshcore.rules /etc/udev/rules.d/
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
sudo install -m 644 variants/linux/meshcored.service /etc/systemd/system/
sudo install -m 644 variants/linux/99-meshcore.rules /etc/udev/rules.d/
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

- **Config path is hardcoded** — meshcored always loads `/etc/meshcored/meshcored.ini`; there is no flag to point it elsewhere. (The ArduLinux runtime itself accepts some flags such as `--fsdir`; how that interacts with the INI's `data_dir` is not yet verified on hardware — TODO.)
- **Only repeater firmware** — there is no `linux_companion` target yet; companion radio support (BLE/serial interface to a phone app) is not implemented for Linux.
- **`formatFileSystem()`** returns `false` (not implemented) — the CLI `format` command will report failure on Linux.
- **No power management** — `board.sleep()` is a no-op; the power-saving loop in `main.cpp` never actually sleeps.
- **Upstream-sync fragility** — the radio wrapper (`LinuxSX1262Wrapper`) implements the `RadioLibWrapper` interface by hand, so an upstream change that adds a pure-virtual method (e.g. `setParams()`) breaks the Linux build until the override is added. Mirror `CustomSX1262Wrapper` when this happens.
