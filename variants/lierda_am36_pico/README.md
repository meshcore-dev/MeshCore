# MeshCore on the Lierda AM36 Pico (L-LRMAM36-FANN4-PK02)

Community variant for the Lierda AM36 Pico evaluation board. The on-board
L-LRMAM36-FANN4 module is an **ESP32-S3** (8MB flash / 8MB PSRAM) paired with a
**Semtech LR2021** Generation-4 LoRa transceiver.

> Status: **hardware-validated** (Jul 2026) against a live US915 MeshCore mesh:
> radio init, noise floor sampling (~-119 dBm), BLE pairing with the iOS
> companion app, flood + direct TX with interrupt-driven send-complete, and
> two-way channel messaging through a third-party repeater
> (SNR up 11.5 dB / down 13.0 dB, replies received at SNR 3.25 dB).
> Requires the `CustomLR2021` radio classes from MeshCore PR #2944.

## Hardware facts used by this variant

| Signal | ESP32-S3 GPIO | Source | Verified |
|---|---|---|---|
| LR2021 SPI SCK | 40 | `esp32_lora_driver` Kconfig (`LIERDA_BOARD_LRMAM36_FANN4`) | yes (on air) |
| LR2021 SPI MOSI | 41 | same | yes |
| LR2021 SPI MISO | 42 | same | yes |
| LR2021 NSS | 39 | same | yes |
| LR2021 NRST | 38 | same | yes |
| LR2021 BUSY | 17 | same | yes |
| LR2021 IRQ | 21 - **wired to LR2021 DIO7** | Kconfig `LR20XX_DIO7_GPIO` | yes (TX-done + RX interrupts) |
| KEY button | 4 (schematic net `GPIO4/KEY1`) | schematic | **not yet** - left disabled |

The IRQ DIO matters: the Seeed Wio-LR2021 (which PR #2944 targets) uses DIO8,
so this variant sets `-D LR2021_IRQ_DIO=7`. No external TCXO or RF-switch
GPIOs exist on the MCU side - the module handles RF internally. The board's
only LED is a hardwired 3V3 power indicator, so no `PIN_STATUS_LED` is defined.

## Build

1. Install VS Code + the PlatformIO extension.
2. Get the source with the LR2021 driver:
   ```bash
   git clone https://github.com/meshcore-dev/MeshCore
   cd MeshCore
   git fetch origin pull/2944/head:lr2021
   git checkout lr2021
   ```
   (Once PR #2944 is merged, plain `dev` works.)
3. Copy this `lierda_am36_pico/` folder into `variants/`.
   (PlatformIO auto-discovers `variants/*/platformio.ini`.)
4. Radio parameters: builds inherit MeshCore's defaults; set your region's
   frequency/BW/SF/CR from the companion app after first boot (settings are
   persisted on the device), or override `LORA_*` flags in a
   `platformio.local.ini` for your own builds. The module covers 863-930 MHz.
5. Build:
   ```bash
   pio run -e Lierda_AM36_Pico_companion_radio_ble
   ```
   Other envs: `..._companion_radio_usb`, `..._repeater`.
   Note: the iOS companion app connects over BLE only, so iPhone users want
   the `_ble` build.

## Flash

1. Connect the Type-C port labelled **USB** (NOT the one labelled UART - that
   goes to the CP2105 bridge). The USB port is the ESP32-S3's native
   USB-JTAG/CDC and is what esptool talks to.
2. ```bash
   pio run -e Lierda_AM36_Pico_companion_radio_ble -t upload
   ```
3. If the port doesn't enumerate or upload fails to sync: hold **BOOT**, tap
   **RST**, release **BOOT** (forces download mode), then retry the upload.
4. Tap **RST** after flashing completes.

## Verify

- Serial console is on the same USB port
  (`pio device monitor -e Lierda_AM36_Pico_companion_radio_ble`). Note the
  ESP32-S3 re-enumerates USB on reset, so the monitor may need a restart after
  tapping RST. A failed radio bring-up prints
  `ERROR: LR2021 init failed: <code>` and halts (no BLE advertising).
- **Attach the LoRa antenna before any TX.**
- Pair from the MeshCore app over Bluetooth (default PIN `123456`, set via
  `BLE_PIN_CODE`), set your region's radio parameters in the app, and send a
  channel message or advert.

## Known quirks / notes

- SPI to the LR2021 sits on GPIO39-42, which are the S3's JTAG pins. That's
  fine (flashing/debug uses USB-JTAG), just don't try external JTAG.
- PSRAM is present but left disabled - MeshCore doesn't need it, and octal
  PSRAM misconfiguration is a classic S3 boot-loop source.
- The KEY button (schematic net `GPIO4/KEY1`) is left disabled until traced
  on hardware; uncomment `PIN_USER_BTN` in `platformio.ini` once confirmed.
- PR #2944's wrapper drops the radio to standby before every `startReceive()`
  (LR2021 wedges if re-armed mid-RX) - don't "optimize" that away.
