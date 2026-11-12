# nRF52 Power Management

## Overview

The nRF52 Power Management module provides battery protection features to prevent over-discharge, minimise likelihood of brownout and flash corruption conditions existing, and enable safe voltage-based recovery.

## Features

### Boot Voltage Protection
- Checks battery voltage immediately after boot and before mesh operations commence
- If voltage is below a configurable threshold (e.g., 3300mV), the device configures voltage wake (LPCOMP + VBUS) and enters protective shutdown (SYSTEMOFF)
- Prevents boot loops when battery is critically low
- Skipped when external power (USB VBUS) is detected

### Voltage Wake (LPCOMP + VBUS)
- Configures the nRF52's Low Power Comparator (LPCOMP) before entering SYSTEMOFF
- Enables USB VBUS detection so external power can wake the device
- Device automatically wakes when battery voltage rises above recovery threshold or when VBUS is detected

### Early Boot Register Capture
- Captures RESETREAS (reset reason) and GPREGRET2 (shutdown reason) before SystemInit() clears them
- Allows firmware to determine why it booted (cold boot, watchdog, LPCOMP wake, etc.)
- Allows firmware to determine why it last shut down (user request, low voltage, boot protection)

### Shutdown Reason Tracking
Shutdown reason codes (stored in GPREGRET2):

| Code | Name         | Description                           |
|------|--------------|---------------------------------------|
| 0x00 | NONE         | Normal boot / no previous shutdown    |
| 0x4C | LOW_VOLTAGE  | Runtime low voltage threshold reached |
| 0x55 | USER         | User requested powerOff()             |
| 0x42 | BOOT_PROTECT | Boot voltage protection triggered     |

## Supported Boards


| Board                                     | Implemented | LPCOMP wake | VBUS wake |
|-------------------------------------------|-------------|-------------|-----------|
| Seeed Studio XIAO nRF52840 (`xiao_nrf52`) | Yes         | Yes         | Yes       |
| RAK4631 (`rak4631`)                       | Yes         | Yes         | Yes       |
| Heltec T114 (`heltec_t114`)               | Yes         | Yes         | Yes       |
| GAT562 Mesh Watch13                       | Yes         | Yes         | Yes       |
| Promicro nRF52840                         | Yes         | Yes         | Yes       |
| RAK WisMesh Tag                           | No          | No          | No        |
| Heltec Mesh Solar                         | No          | No          | No        |
| LilyGo T-Echo / T-Echo Lite               | No          | No          | No        |
| SenseCAP Solar                            | Yes         | Yes         | Yes       |
| WIO Tracker L1 / L1 E-Ink                 | No          | No          | No        |
| WIO WM1110                                | No          | No          | No        |
| Mesh Pocket                               | No          | No          | No        |
| Nano G2 Ultra                             | No          | No          | No        |
| ThinkNode M1/M3/M6                        | No          | No          | No        |
| T1000-E                                   | No          | No          | No        |
| Ikoka Nano/Stick/Handheld (nRF)           | No          | No          | No        |
| Keepteen LT1                              | No          | No          | No        |
| Minewsemi ME25LS01                        | No          | No          | No        |

Notes:
- Promicro also checks battery voltage every 5 seconds while running. Three
  consecutive readings below 3100 mV trigger peripheral shutdown and SYSTEMOFF
  with LPCOMP recovery enabled. USB power bypasses this runtime check.
- On Promicro, `PWRMGT_VOLTAGE_BOOTLOCK` and `PWRMGT_VOLTAGE_SHUTDOWN` are ADC
  thresholds, not the LPCOMP wake threshold. The retained empirical setting
  `PWRMGT_LPCOMP_REFSEL=4` selects 5/8 VDD; measure VDD and the battery divider
  to determine the actual recovery voltage. SYSTEMOFF does not disconnect the
  battery from the board's other loads.
- Promicro releases radio SPI, reset, RF-switch and status pins to inputs
  without pulls before disabling external VCC, to avoid feeding the unpowered
  module through GPIO signals. Runtime shutdown also stops SPI first; boot
  lockout skips SPI uninitialization because SPI has not been started yet.
- "Implemented" reflects Phase 1 (boot lockout + shutdown reason capture),
  with the additional runtime cutoff described above for Promicro.
- User power-off on Heltec T114 does not enable LPCOMP wake.
- VBUS detection is used to skip boot lockout on external power, and VBUS wake is configured alongside LPCOMP when supported hardware exposes VBUS to the nRF52.

## Technical Details

### Architecture

The power management functionality is integrated into the `NRF52Board` base class in `src/helpers/NRF52Board.cpp`. Board variants provide hardware-specific configuration via a `PowerMgtConfig` struct and override `initiateShutdown(uint8_t reason)` to perform board-specific power-down work and conditionally enable voltage wake (LPCOMP + VBUS).

### Early Boot Capture

A static constructor with priority 101 in `NRF52Board.cpp` captures the RESETREAS and GPREGRET2 registers before:
- SystemInit() (priority 102) - which clears RESETREAS
- Static C++ constructors (default priority 65535)

This ensures we capture the true reset reason before any initialisation code runs.

### Board Implementation

To enable power management on a board variant:

1. **Enable in platformio.ini**:
   ```ini
   -D NRF52_POWER_MANAGEMENT
   ```

2. **Define configuration in variant.h**:
   ```c
   #define PWRMGT_VOLTAGE_BOOTLOCK    3300   // Won't boot below this voltage (mV)
   #define PWRMGT_LPCOMP_AIN          7      // AIN channel for voltage sensing
   #define PWRMGT_LPCOMP_REFSEL       2      // REFSEL (0-6=1/8..7/8, 7=ARef, 8-15=1/16..15/16)
   ```

3. **Implement in board .cpp file**:
   ```cpp
   #ifdef NRF52_POWER_MANAGEMENT
   const PowerMgtConfig power_config = {
     .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
     .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
     .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
   };

   void MyBoard::initiateShutdown(uint8_t reason) {
     // Board-specific shutdown preparation (e.g., disable peripherals)
     bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                           reason == SHUTDOWN_REASON_BOOT_PROTECT);

     if (enable_lpcomp) {
       configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
     }

     enterSystemOff(reason);
   }
   #endif

   void MyBoard::begin() {
     NRF52Board::begin();  // or NRF52BoardDCDC::begin()
     // ... board setup ...

   #ifdef NRF52_POWER_MANAGEMENT
     checkBootVoltage(&power_config);
   #endif
   }
   ```

   For user-initiated shutdowns, `powerOff()` remains board-specific. Power management only arms LPCOMP for automated shutdown reasons (boot protection/low voltage).

4. **Declare override in board .h file**:
   ```cpp
   #ifdef NRF52_POWER_MANAGEMENT
     void initiateShutdown(uint8_t reason) override;
   #endif
   ```

### Voltage Wake Configuration

The LPCOMP (Low Power Comparator) is configured to:
- Monitor the specified AIN channel (0-7 corresponding to P0.02-P0.05, P0.28-P0.31)
- Compare against VDD fraction reference (REFSEL: 0-6=1/8..7/8, 7=ARef, 8-15=1/16..15/16)
- Detect UP events (voltage rising above threshold)
- Use no comparator hysteresis (as of firmware 1.18)
- Wake the device from SYSTEMOFF when triggered

VBUS wake is enabled via the POWER peripheral USBDETECTED event whenever `configureVoltageWake()` is used. This requires USB VBUS to be routed to the nRF52 (typical on nRF52840 boards with native USB).

**LPCOMP Reference Selection (PWRMGT_LPCOMP_REFSEL)**:

| REFSEL | Fraction | VBAT @ 1M/1M divider (VDD=3.0-3.3) | VBAT @ 1.5M/1M divider (VDD=3.0-3.3) |
|--------|----------|------------------------------------|--------------------------------------|
| 0      | 1/8      | 0.75-0.82 V                        | 0.94-1.03 V                          |
| 1      | 2/8      | 1.50-1.65 V                        | 1.88-2.06 V                          |
| 2      | 3/8      | 2.25-2.47 V                        | 2.81-3.09 V                          |
| 3      | 4/8      | 3.00-3.30 V                        | 3.75-4.12 V                          |
| 4      | 5/8      | 3.75-4.12 V                        | 4.69-5.16 V                          |
| 5      | 6/8      | 4.50-4.95 V                        | 5.62-6.19 V                          |
| 6      | 7/8      | 5.25-5.77 V                        | 6.56-7.22 V                          |
| 7      | ARef     | -                                  | -                                    |
| 8      | 1/16     | 0.38-0.41 V                        | 0.47-0.52 V                          |
| 9      | 3/16     | 1.12-1.24 V                        | 1.41-1.55 V                          |
| 10     | 5/16     | 1.88-2.06 V                        | 2.34-2.58 V                          |
| 11     | 7/16     | 2.62-2.89 V                        | 3.28-3.61 V                          |
| 12     | 9/16     | 3.38-3.71 V                        | 4.22-4.64 V                          |
| 13     | 11/16    | 4.12-4.54 V                        | 5.16-5.67 V                          |
| 14     | 13/16    | 4.88-5.36 V                        | 6.09-6.70 V                          |
| 15     | 15/16    | 5.62-6.19 V                        | 7.03-7.73 V                          |

**Important**: For boards with a voltage divider on the battery sense pin, LPCOMP measures the divided voltage. Use:
`VBAT_threshold ≈ (VDD * fraction) * divider_scale`, where `divider_scale = (Rtop + Rbottom) / Rbottom` (e.g., 2.0 for 1M/1M, 2.5 for 1.5M/1M, 3.0 for XIAO).

### SoftDevice Compatibility

The power management code checks whether SoftDevice is enabled and uses the appropriate API:
- When SD enabled: `sd_power_*` functions
- When SD disabled: Direct register access (NRF_POWER->*)

This ensures compatibility regardless of BLE stack state.

## CLI Commands

Power management status can be queried via the CLI:

| Command                 | Description                                                           |
|-------------------------|-----------------------------------------------------------------------|
| `get pwrmgt.support`    | Returns "supported" or "unsupported"                                  |
| `get pwrmgt.source`     | Returns current power source - "battery" or "external" (5V/USB power) |
| `get pwrmgt.bootreason` | Returns reset and shutdown reason strings                             |
| `get pwrmgt.bootmv`     | Returns boot voltage in millivolts                                    |

On boards without power management enabled, `get pwrmgt.source` and
`get pwrmgt.bootmv` return:
```
ERROR: Power management not supported
```

`get pwrmgt.support` returns `unsupported`. `get pwrmgt.bootreason` is not
compiled out at all and answers on every board: `getResetReason()` and
`getShutdownReason()` are virtuals on the base board class, so a board that
does not override them reports `Not available` rather than an error. ESP32
boards override the reset half with `esp_reset_reason()`, so they return a real
reset reason and `Not available` for the shutdown reason.

## Promicro / Faketec hardware and configuration

The standard Promicro environments use the bench-tested battery divider below.
Replace both original `1002` (10 kOhm) resistors before using this configuration:

```text
BAT+ --- 160 kOhm ---+--- P0.31 / AIN7 (Arduino pin 17)
                    |
                  240 kOhm
                    |
                   GND
```

Confirm which resistor connects to BAT+ and which connects to GND before
replacement. This is a hardware-specific configuration, not a firmware-only
upgrade for an unchanged 10k/10k board. The divider draws approximately 8.25 uA
at 3.3 V, versus 165 uA for the original divider. Resistor tolerances and ADC
errors still require checking the reported voltage against a meter.

| Setting | Default |
| --- | --- |
| ADC multiplier | 1.46484375 mV/count = (3600 / 4096) x (160 + 240) / 240 |
| ADC resolution / acquisition | 12 bits / 40 us |
| Samples per voltage reading | 8 |
| Boot lock / runtime shutdown | 3100 mV / 3100 mV |
| Runtime monitoring | Every 5 s; three consecutive low readings |
| LPCOMP input / reference | AIN7 / REFSEL=4, 5/8 VDD |

Any saved `adc.multiplier` overrides the compiled default after preferences
are loaded, while the early boot check uses the compiled default. After the
hardware/firmware update, run `set adc.multiplier 0` to restore the board default
and compare the reading with a meter before testing shutdown. Only save a new
calibration after checking several voltages.

Runtime shutdown first stops peripherals. Before disabling external VCC via
P0.13, the board stops SPI and releases radio SPI, NSS, RESET, BUSY, DIO1 and
RXEN pins to inputs without pulls. GPIO state is retained in SYSTEMOFF; leaving
NSS/RESET driven high was consistent with backfeeding the unpowered radio.
Boot lockout applies the same pin isolation but skips SPI uninitialization,
because SPI has not been initialized at that point.

LPCOMP and VBUS recovery are armed for low-voltage and boot-protection shutdowns.
Manual `powerOff()` remains a user shutdown without LPCOMP recovery. USB power
bypasses the boot voltage check and runtime low-voltage shutdown.

### Bench validation (2026-11-13)

Results reported on one Promicro/Faketec assembly using a bench supply, with
battery disconnected. The final divider test used the repeater without debug
logging and with GPS disabled. Standard environments retain their existing GPS
configuration; GPS operation and additional peripheral loads were not validated
by these tests.

| Measurement / test | Observed result |
| --- | --- |
| Final 160k/240k divider, REFSEL=4, firmware threshold 3100 mV | Shutdown at 3.08 V actual supply voltage |
| Recovery with the same configuration | Wake at 3.41 V actual supply voltage |
| Margin between observed shutdown and recovery | Approximately 330 mV |
| Battery voltage indication | Approximately 10 mV difference from supply/meter |
| Radio VCC and RESET after shutdown pin isolation | Fell from approximately 2.56 V to 0.02 V |
| P0.13 after shutdown pin isolation | 0 V (previously approximately 0.52 V) |
| Total shutdown current | Supply displayed 0.000 A, below its approximately 1 mA resolution; not a measured zero-current result |
| Earlier shutdown current before pin isolation | Approximately 7 mA |

Diagnostic logs confirmed `Wake from LPCOMP` (RESETREAS 0x20000), previous
shutdown `Low Voltage` (0x4C), and successful radio initialization after wake.
VBUS wake was also reported working during the earlier diagnostic tests. Repeat
LPCOMP and VBUS tests after flashing the cleaned standard build; no hardware
validation of that rebuilt artifact is implied by successful compilation.

With the earlier 150k/240k divider, REFSEL=4 recovered at 3.11-3.15 V, leaving
only 40-60 mV of observed margin. REFSEL=13 (11/16 VDD) raised recovery to
3.6-3.7 V. The final 160k/240k divider with REFSEL=4 provided the preferred
observed margin. LPCOMP compares against a fraction of VDD, not an absolute
battery reference: measure recovery on each hardware revision rather than
assuming an exact threshold from indirect VDD measurements.

For repeat tests, remove board USB and the UART adapter, lower the supply slowly
and allow at least 20 s at each step, then raise it slowly without interrupting
power. Measure actual voltage, current and wake stability under load. When
checking VBUS wake, ensure USB charging cannot drive reverse current into the
bench supply. SYSTEMOFF reduces consumption but does not physically disconnect
the battery or replace hardware battery protection.

## Debug Output

When `MESH_DEBUG=1` is enabled, the power management module outputs:
```
DEBUG: PWRMGT: Reset = Wake from LPCOMP (0x20000); Shutdown = Low Voltage (0x4C)
DEBUG: PWRMGT: Boot voltage = 3450 mV (threshold = 3300 mV)
DEBUG: PWRMGT: LPCOMP wake configured (AIN7, ref=3/8 VDD)
```

## Phase 2 (Planned)

- General runtime voltage monitoring (Promicro already implements the fixed policy above)
- Voltage state machine (Normal -> Warning -> Critical -> Shutdown)
- Configurable thresholds
- Load shedding callbacks for power reduction
- Deep sleep integration
- Scheduled wake-up
- Extended sleep with periodic monitoring

## References

- [nRF52840 Product Specification - POWER](https://infocenter.nordicsemi.com/topic/ps_nrf52840/power.html)
- [nRF52840 Product Specification - LPCOMP](https://infocenter.nordicsemi.com/topic/ps_nrf52840/lpcomp.html)
- [SoftDevice S140 API - Power Management](https://infocenter.nordicsemi.com/topic/sdk_nrf5_v17.1.0/group__nrf__sdm__api.html)
