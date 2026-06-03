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
- **First-run node defaults**: `advert_name`, `admin_password`, `lat`, `lon`. On the first boot these are saved to the node's persisted prefs (`com_prefs`). After that, use the serial CLI to change them (`set name`, `set password`, etc.) — the INI values are no longer consulted for these fields.

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
| `dio2_as_rf_switch` | `0` | `1` = use DIO2 to drive the TX/RX RF switch. **Required for the Waveshare Core1262** (without it the radio inits but TX/RX are dead); depends on module wiring |
| `rx_boosted_gain` | `1` | `1` enables the SX126x RX boosted-gain mode; `0` disables |
| `advert_name` | `"Linux Repeater"` | Node name — first-run default only |
| `admin_password` | `"password"` | Admin password — **change this**, first-run default only |
| `lat` / `lon` | `0.0` | GPS coordinates for advertisement — first-run default only |

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
>
> **Arch Linux kernel caveat:** `dtparam=spi=on` is only honored by the Raspberry
> Pi `linux-rpi` (vendor) kernel. The mainline `linux-aarch64` kernel boots via
> U-Boot, which loads its own device tree and ignores `config.txt` overlays — so
> `/dev/spidev*` never appears regardless of `config.txt`. If SPI is missing after
> enabling it and rebooting, switch to the vendor kernel
> (`sudo pacman -S linux-rpi`, remove `linux-aarch64`) and reboot.

Then grant non-root access to the SPI and GPIO devices using the provided udev
rules, which place `/dev/spidev*` and `/dev/gpiochip*` in a `meshcore` group.
Create the group, add yourself to it, and install the rules:

```sh
sudo groupadd -f -r meshcore
sudo usermod -aG meshcore "$USER"     # log out/in afterwards for this to take effect
sudo install -m 644 variants/linux/99-meshcore.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Confirm the device nodes are now group-owned by `meshcore`:

```sh
ls -l /dev/gpiochip* /dev/spidev*    # → crw-rw---- root meshcore
```

Your current login session won't pick up the new group until you log out and
back in. To use it immediately in one shell, prefix the command with
`sg meshcore -c '…'`. (On Raspberry Pi OS you can instead use the built-in
`spi`/`gpio` groups: `sudo usermod -aG spi,gpio $USER`.)

### 4. Run

**Directly** (for testing). With the udev rules in place you can run as your own
user — no `sudo`. Data is persisted under the VFS root, which defaults to the XDG
data dir; pass `--fsdir` to choose another location:

```sh
meshcored                              # VFS root: ~/.local/share/meshcored/default
meshcored --fsdir /var/lib/meshcore    # explicit location
# before re-logging in (group not yet active in this shell):
sg meshcore -c 'meshcored --fsdir /var/lib/meshcore'
```

**As a systemd service** (recommended for production). The unit runs as the
`meshcore` user and passes `--fsdir /var/lib/meshcore`:

```sh
sudo install -m 644 variants/linux/meshcored.service /etc/systemd/system/
sudo install -m 644 variants/linux/99-meshcore.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo useradd -r -s /sbin/nologin meshcore
sudo chmod 640 /etc/meshcored/meshcored.ini
sudo chown root:meshcore /etc/meshcored/meshcored.ini
sudo systemctl daemon-reload
sudo systemctl enable --now meshcored
sudo journalctl -u meshcored -f
```

> The unit's `ExecStartPre` creates and chowns `/var/lib/meshcore`, so you don't
> need to pre-create it. If you smoke-tested by running directly first, clear any
> stale state so the service first-boots with the INI defaults:
> `sudo rm -rf /var/lib/meshcore/*`

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

- **Config path is hardcoded** — meshcored always loads `/etc/meshcored/meshcored.ini`; there is no flag to point it elsewhere. (The data *path* is separate and configurable: it is the ArduLinux VFS root, set with `--fsdir`.)
- **Only repeater firmware** — there is no `linux_companion` target yet; companion radio support (BLE/serial interface to a phone app) is not implemented for Linux.
- **`formatFileSystem()`** returns `false` (not implemented) — the CLI `format` command will report failure on Linux.
- **No power management** — `board.sleep()` is a no-op; the power-saving loop in `main.cpp` never actually sleeps.
- **Upstream-sync fragility** — the radio wrapper (`LinuxSX1262Wrapper`) implements the `RadioLibWrapper` interface by hand, so it can drift from upstream in two ways: a new **pure-virtual** method breaks the Linux build (e.g. `setParams()`), and a new **virtual-with-default** method silently no-ops on Linux until overridden (e.g. `set`/`getRxBoostedGainMode()`, which reported and applied the wrong state until added). Mirror `CustomSX1262Wrapper` when syncing.
