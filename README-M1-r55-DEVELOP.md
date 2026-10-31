# MeshCore M1 r55 — ready-to-develop source package

This directory is the complete PlatformIO project for ThinkNode M1 r55. It contains:

- `variants/thinknode_m1/`: ThinkNode M1 V1.5 / V2.0 support, including automatic
  compatibility with both the old and the new GPS module.
- `examples/companion_radio/`: application, UI, deep sleep, low-voltage protection and
  status-LED logic.
- `src/`, `lib/`, `boards/`: the MeshCore project itself.
- `third_party/Adafruit_nRF52_Arduino-d541301/`: the verified Adafruit nRF52 Arduino
  framework sources.

## Building on Windows

PlatformIO Core (or the PlatformIO IDE) and an ARM toolchain are required. The project
already points at the bundled Adafruit framework through `platformio.local.ini`.

Open PowerShell in this directory:

```powershell
python -m platformio run -e ThinkNode_M1_companion_radio_ble -t create_uf2
```

Output files:

```text
.pio/build/ThinkNode_M1_companion_radio_ble/firmware.uf2
.pio/build/ThinkNode_M1_companion_radio_ble/firmware.hex
```

If the ARM toolchain is not cached on the machine, PlatformIO downloads
`platformio/toolchain-gccarmnoneeabi@^1.140201.0` automatically. You can also open this
directory in the PlatformIO IDE and select the `ThinkNode_M1_companion_radio_ble`
environment.

## r55 behaviour baseline

- The GPS page shows `GPS ON` / `GPS OFF`. On V2.0, enabling GPS no longer blocks key
  handling for a full probe: the UART probe runs in the background.
- On the last page (`hibernate: long press`), a 3-second long press of the page key enters
  deep sleep. All other pages keep the r49 key behaviour.
- Below 3.5 V the red LED blinks at 1 Hz; below 3.45 V for 10 consecutive checks
  (30 s apart) the device enters deep sleep; below 3.28 V twice, 10 s apart, it powers off.
- Transmitting does not light the blue LED; a valid received packet gives one short blue
  pulse.
- For normal deep sleep either key can wake the device. After a critical low-voltage
  power-off the knob must be switched off and on again, or USB has to be connected, to
  start the device.

## Version and verification

Corresponding firmware: `dist-final55/ThinkNode_M1_companion_radio_ble-v1.17.1-m1-final55.uf2`
(if you only received the source package, the firmware lives in the `dist-final55`
directory next to this package).

r55 has been built locally with PlatformIO and the UF2 image has been format-verified.
It has not yet been re-tested on hardware by the recipient of this source package. The
package does not contain `.git` history, `.pio` build caches, or absolute paths from the
development machine.
