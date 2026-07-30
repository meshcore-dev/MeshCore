# MeshCore companion — Seeed XIAO nRF54L15 + LR2021 (Zephyr)

A MeshCore **companion-radio** node (the `examples/companion_radio` firmware, BLE-paired to the
MeshCore phone app) running on the Seeed Studio XIAO nRF54L15 with a Semtech LR2021 radio, built on
**mainline Zephyr** (not NCS). The MeshCore core, helpers, and the companion app are compiled
straight from this repo; the radio driver is stock RadioLib in generic (non-Arduino) mode.

## What's in here

| Path | Role |
|---|---|
| `src/main.cpp` | Zephyr entry point; runs the MeshCore companion loop |
| `src/target.cpp`, `src/target.h` | board/radio bring-up: pin map, band presets, TX-power clamp; radio = the shared `CustomLR2021`/`CustomLR2021Wrapper` |
| `src/serial_ble_interface.cpp/.h` | BLE transport: custom encryption-gated Nordic-UART (NUS) GATT service; central-driven pairing |
| `src/zephyr_internal_fs.cpp` | `InternalFileSystem` backed by a Zephyr flash partition (LittleFS) for prefs/identity |
| `compat/` | header-only shims (CayenneLPP, RTClib, Arduino glue) so the core builds without those libs |
| `dts/`, `app.overlay`, `prj.conf` | devicetree overlay (SPI/GPIO for the LR2021) and Kconfig |

All LR2021-specific radio fixes (standby before re-arming RX, header-CRC recovery, RX
max-length re-assert) live in `src/helpers/radiolib/CustomLR2021.h` /
`CustomLR2021Wrapper.h` at the repo root, shared with the bare-metal variant
(`variants/xiao_nrf54l15`). The core `RadioLibWrapper` exposes a neutral
`onBeforeStartRecv()` hook for the standby quirk, so other radios keep stock behaviour.

## Prerequisites

1. **Mainline Zephyr 4.4.99** (has the `xiao_nrf54l15` board) + its Python venv and the matching
   **Zephyr SDK**. A typical layout:
   - `ZEPHYR_BASE=$HOME/zephyrproject/zephyr`
   - west + deps in a venv, e.g. `$HOME/.zephyr-venv`
2. **Arduino libraries** under `$HOME/Arduino/libraries/` (the `CMakeLists.txt` references them by
   that absolute path). Only three are actually compiled/included:
   - `RadioLib/` — the LR2021 driver (generic HAL; the Arduino HAL is excluded by the build)
   - `Crypto/`
   - `base64/`

   (`CayenneLPP`, `RTClib`, `ArduinoJson` are *not* needed — they are shimmed in `compat/`.)
   `ed25519` ships in the repo (`lib/ed25519`).

## Build

From this directory (`zephyr-port/07_companion`):

```sh
source $HOME/.zephyr-venv/bin/activate
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr

west build -b xiao_nrf54l15/nrf54l15/cpuapp -d build . --pristine
```

Default band is **sub-GHz (869.618 MHz / 62.5 kHz / SF8 / CR5)**. For the **2.4 GHz** preset
(2450 MHz / 500 kHz / SF8 / CR5, TX clamped to +12 dBm) add:

```sh
west build -b xiao_nrf54l15/nrf54l15/cpuapp -d build . --pristine -- -DMC_BAND_2G4=ON
```

`MC_BAND_2G4` only sets the **first-boot** band; the running band/SF/BW/CR is also stored in prefs
and can be changed live from the app (`CMD_SET_RADIO_PARAMS`); and that choice now persists across
reboots. Two nodes must be on the same band to hear each other.

## Flash

The XIAO's on-board debugger enumerates as a **CMSIS-DAP** probe, so the simplest method is
**pyocd**. The pyocd target name is **`nrf54l`** (not `nrf54l15`), and `-e chip` does a clean erase:

```sh
pyocd flash -t nrf54l -e chip build/zephyr/zephyr.hex
pyocd reset -t nrf54l
```

With more than one board attached, add `-u <PROBE_UID>` (from `pyocd list`) so you flash the right
one — `-e chip` erases whatever you point it at.

`west flash` also works if you have J-Link or OpenOCD set up (the board defines both runners; there
is no pyocd runner):

```sh
west flash -d build --runner jlink      # or: --runner openocd
```

## Monitor (console / BLE PIN)

The nRF54L15 has **no USB**, so the console (boot log, the BLE PIN, and the `FS:` / `PREFS:` /
`radio_set_params` diagnostics) is read over the SWD probe via **RTT**. The default build sends the
console to a UART; to get RTT, build with a small overlay and read it with pyocd:

```sh
# rtt.conf
CONFIG_USE_SEGGER_RTT=y
CONFIG_RTT_CONSOLE=y
CONFIG_UART_CONSOLE=n

west build -b xiao_nrf54l15/nrf54l15/cpuapp -d build . -- -DEXTRA_CONF_FILE=rtt.conf
pyocd rtt -t nrf54l
```

## Pair

Advertises as **`MeshCore-<node name>`**; e.g. `MeshCore-544BA815` (the pubkey-derived default
name) or `MeshCore-NRF54L15-1` after you rename it; a rename from the app updates the advertised
name **live**, no reboot. In the MeshCore phone app, add a new companion device and pair; the node
uses a fixed passkey **`123456`** (printed on the RTT console at boot). Pairing is **central-driven**
(the app initiates encryption on first access to the NUS characteristics), which keeps iOS/Windows
from dropping the link right after the PIN.

## Notes / limitations

- **Prefs, identity, and contacts persist across power loss; BLE bonds do not**
  (`CONFIG_BT_SETTINGS` off). After a node reboot a previously-paired phone must **forget & re-pair**
  (it tries to resume a bond the node no longer holds → `0x13` disconnect loop). The correct settings
  backend for RRAM is **ZMS** (not NVS; RRAM has no erase); a 12 KB `settings_partition` is already
  reserved in `app.overlay` for when this is enabled. Future work (see `prj.conf`).
- **Filesystem persistence depends on `read_size == prog_size == block_size`** in
  `zephyr_internal_fs.cpp` (matches the proven bare-metal `helpers/nrf54` FS). With a smaller
  prog/read size, littlefs-v1's in-block commit-log path does **not** read back after a cold boot on
  RRAM; the FS remounts to its empty post-format state, so identity/prefs silently reset every boot.
  Erase is a no-op (RRAM is byte-alterable).
- **`CONFIG_BT_CTLR_ASSERT_OVERHEAD_START=n`** (which needs `CONFIG_BT_CTLR_ADVANCED_FEATURES=y` to be
  overridable; it lives in a `visible if` menu). The first BLE advertising event races the boot
  bring-up (radio SPI + FS mount) and runs tens of ms late; left at its default `y` the controller
  turns that into a **fatal** assert that takes the whole device (and the LoRa mesh) down. Disabled,
  the controller just skips the late event and continues. Likewise, a runtime band change
  (`radio_set_params`) is kept lightweight so it can't starve the live BLE connection into a fault.
- The NUS characteristics require **authenticated (MITM) pairing** (`BT_GATT_PERM_WRITE_AUTHEN`); a
  central that only does Just Works would be rejected. Relax to `_ENCRYPT` in
  `serial_ble_interface.cpp` if needed.
- RRAM writes run synchronously (`CONFIG_SOC_FLASH_NRF_RADIO_SYNC_NONE`) to avoid a ~24 s
  settings-write stall while BLE-connected.
- The board has no chip-controlled TCXO; the radio runs on the crystal (`tcxoVoltage = 0`).
