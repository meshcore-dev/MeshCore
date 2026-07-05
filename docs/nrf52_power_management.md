# nRF52 Power Management

## Overview

The nRF52 Power Management module provides battery protection features to prevent over-discharge, minimise likelihood of brownout and flash corruption conditions existing, and enable safe voltage-based recovery.

## Features

### Boot Voltage Protection
- Checks battery voltage immediately after boot and before mesh operations commence
- If voltage is below a configurable threshold (e.g., 3300mV), and the board declares the battery sense path valid, the device configures voltage wake and enters protective shutdown (SYSTEMOFF)
- Prevents boot loops when battery is critically low
- Skipped when external power (USB VBUS) is detected, when the battery voltage sense path is invalid, when the battery reading is below the configured present threshold, or when the battery voltage evidence is implausibly high

### Voltage Wake (LPCOMP + VBUS)
- Configures the nRF52's Low Power Comparator (LPCOMP) before entering SYSTEMOFF only when the board declares the LPCOMP sense node valid
- Enables USB VBUS detection independently where hardware supports VBUS wake
- Device automatically wakes when battery voltage rises above recovery threshold or when VBUS is detected

### Runtime Power-Fail Shutdown
- Optionally arms the nRF52 power-fail warning comparator on regulated VDD
- Allows firmware to enter SYSTEMOFF before an uncontrolled brownout if VDD falls through the configured threshold
- Can arm VBUS detection as the recovery wake source for builds powered by a battery connected to VUSB
- Does not replace hardware brownout behaviour: if voltage collapses before firmware runs the handler, recovery is controlled by reset and regulator hardware

### Power Source State
- `get pwrmgt.source` returns a source state. Normal detected sources use a confidence suffix: `vusb+bat`, `vusb-only`, `bat-only`, or `none`, followed by `:valid`, `:implausible`, `:invalid`, or `:unknown`
- `invalid` means the board cannot use the configured battery sense path for protective decisions
- `implausible` means the sensed voltage is outside the board's configured plausible range
- `possible-battery` means the board is powered without VBUS detect, and the board cannot prove whether VUSB is being used as a battery input

Confidence suffixes are assigned as follows:

| Suffix          | Condition                                                                                 | Protective BAT decisions |
|-----------------|-------------------------------------------------------------------------------------------|--------------------------|
| `:valid`        | Battery sense is enabled for the board and the reading is within the configured range      | Allowed                  |
| `:implausible`  | Battery sense is enabled, the reading is present, and it is below the plausible range or above maximum | Present low readings still shut down; absent/floating and high readings are blocked |
| `:invalid`      | Battery sense is not valid for the board's supported wiring or operating mode              | Blocked                  |
| `:unknown`      | Power management has no active board configuration when the source is queried              | Blocked                  |
| `:possible-battery` | VBUS detect is low, BAT sense is invalid, and VUSB may be acting as a battery input   | Blocked                  |

The current nRF52 power-management board configs all use these plausibility thresholds:

| `PowerMgtConfig` field       | Configured value | Meaning                                      |
|------------------------------|------------------|----------------------------------------------|
| `battery_min_present_mv`     | `1000`           | Readings below 1000mV are treated as absent/floating and skipped for boot protection |
| `battery_min_plausible_mv`   | `2500`           | Readings below 2500mV are reported as `:implausible`; readings at or above 1000mV still trigger boot protection when below `voltage_bootlock` |
| `battery_max_plausible_mv`   | `4500`           | Readings above 4500mV are `:implausible` and skipped for boot protection |

The confidence range is inclusive: `2500mV <= battery_mv <= 4500mV` is `:valid` when the board's battery sense path is enabled. Boot protection uses the lower present threshold separately: `1000mV <= battery_mv < voltage_bootlock` enters protective shutdown. Boards can override these fields in their own `PowerMgtConfig`.

Readings below `battery_min_present_mv` are treated as no BAT source. With VBUS detected, the source is reported as `vusb-only:valid`; without VBUS detect, the source is reported as `vusb-only:possible-battery` because the board is still powered and VUSB may be acting as the battery input.

There are no configured VUSB millivolt confidence thresholds in the current implementation. VUSB state is based on the nRF52 `USBREGSTATUS.VBUSDETECT` hardware signal, not a firmware ADC voltage reading centred around 5V. `PowerMgtConfig::vbus_wake_valid` only records whether VBUS wake is supported for the board.

This means a battery connected to VUSB is reported as `vusb-only:valid` while `VBUSDETECT` is asserted. If that VUSB battery falls below the hardware detection point but still powers the MCU, firmware reports `vusb-only:possible-battery` rather than `none`.

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


| Board                                     | Implemented | LPCOMP wake | VBUS wake | Runtime POF shutdown |
|-------------------------------------------|-------------|-------------|-----------|----------------------|
| Seeed Studio XIAO nRF52840 (`xiao_nrf52`) | Yes         | No          | Yes       | Yes                  |
| RAK4631 (`rak4631`)                       | Yes         | Yes         | Yes       | No                   |
| Heltec T114 (`heltec_t114`)               | Yes         | Yes         | Yes       | No                   |
| GAT562 Mesh Watch13                       | Yes         | Yes         | Yes       | No                   |
| Promicro nRF52840                         | No          | No          | No        | No                   |
| RAK WisMesh Tag                           | No          | No          | No        | No                   |
| Heltec Mesh Solar                         | No          | No          | No        | No                   |
| LilyGo T-Echo / T-Echo Lite               | No          | No          | No        | No                   |
| SenseCAP Solar                            | Yes         | Yes         | Yes       | No                   |
| WIO Tracker L1 / L1 E-Ink                 | No          | No          | No        | No                   |
| WIO WM1110                                | No          | No          | No        | No                   |
| Mesh Pocket                               | No          | No          | No        | No                   |
| Nano G2 Ultra                             | No          | No          | No        | No                   |
| ThinkNode M1/M3/M6                        | No          | No          | No        | No                   |
| T1000-E                                   | No          | No          | No        | No                   |
| Ikoka Nano/Stick/Handheld (nRF)           | No          | No          | No        | No                   |
| Keepteen LT1                              | No          | No          | No        | No                   |
| Minewsemi ME25LS01                        | No          | No          | No        | No                   |

Notes:
- "Implemented" reflects Phase 1 (boot lockout + shutdown reason capture).
- User power-off on Heltec T114 does not enable LPCOMP wake.
- VBUS detection is used to skip boot lockout on external power. VBUS wake is configured independently from LPCOMP where supported hardware exposes VBUS to the nRF52.
- XIAO nRF52 disables trusted BAT/LPCOMP protection by default because BAT+ may be disconnected while VUSB is the actual supply.
- Runtime POF shutdown uses the nRF52 power-fail warning comparator. On XIAO USB builds it is configured for regulated VDD at 2.8 V and arms VBUS wake for SYSTEMOFF recovery. XIAO BLE companion builds leave direct POF disabled because SoftDevice is enabled after `board.begin()` and owns POWER events once started. USB builds that later enter OTA disable the direct POF handler before starting SoftDevice.

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
   #define PWRMGT_POWER_FAIL_VDD_THRESHOLD POWER_POFCON_THRESHOLD_V28 // Optional; 2.8 V regulated VDD
   ```

3. **Implement in board .cpp file**:
   ```cpp
   #ifdef NRF52_POWER_MANAGEMENT
   const PowerMgtConfig power_config = {
     .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
     .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
     .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK,
     .battery_voltage_sense_valid = true,
     .lpcomp_voltage_wake_valid = true,
     .vbus_wake_valid = true,
     .battery_min_present_mv = 1000,
     .battery_min_plausible_mv = 2500,
     .battery_max_plausible_mv = 4500,
     .power_fail_vdd_threshold = 0,    // Optional; 0 disables runtime POF shutdown
     .power_fail_vbus_wake = false     // Optional; true arms VBUS wake after POF shutdown
   };

   void MyBoard::initiateShutdown(uint8_t reason) {
     // Board-specific shutdown preparation (e.g., disable peripherals)
     bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                           reason == SHUTDOWN_REASON_BOOT_PROTECT);

     if (enable_lpcomp) {
       configureVoltageWake(&power_config);
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
- Use 50mV hysteresis for noise immunity
- Wake the device from SYSTEMOFF when triggered

VBUS wake is enabled via the POWER peripheral USBDETECTED event whenever `configureVoltageWake()` is used. This requires USB VBUS to be routed to the nRF52 (typical on nRF52840 boards with native USB).

### Runtime Power-Fail Configuration

Runtime power-fail shutdown is configured by optional `PowerMgtConfig` fields:

| Field | Default | Description |
|-------|---------|-------------|
| `power_fail_vdd_threshold` | `0` | Disabled when `0`; otherwise an nRF52 `POWER_POFCON_THRESHOLD_*` value for regulated VDD |
| `power_fail_vbus_wake` | `false` | When true, the POF handler arms VBUS detect as the SYSTEMOFF wake source |

For nRF52840 VDD, supported POF thresholds are 1.7 V through 2.8 V in 0.1 V steps. The XIAO nRF52840 USB-build default uses `POWER_POFCON_THRESHOLD_V28`, the highest available regulated-VDD threshold, so firmware gets the earliest available warning when the 3.3 V rail starts to collapse.

For nRF52840 VDDH, hardware also supports 2.7 V through 4.2 V thresholds. MeshCore does not currently use the VDDH threshold for XIAO because a battery on the XIAO VUSB pin is not the same as direct nRF52840 VDDH measurement in the board abstraction.

This path is deliberately separate from SYSTEMOFF wake source selection:
- If supply voltage simply collapses, firmware does not choose a wake source; reset and regulator hardware determine when execution resumes.
- POF shutdown only applies if the MCU is still executing when VDD crosses the configured threshold.
- When POF shutdown succeeds and `power_fail_vbus_wake` is true, recovery happens when the nRF52 VBUS detector sees VUSB again, not when BAT sense rises.
- SoftDevice builds need a SoftDevice SoC event hook for `NRF_EVT_POWER_FAILURE_WARNING`. The current Adafruit nRF52 framework consumes that event internally, so this branch only enables direct POF shutdown while SoftDevice is not active and disables it before OTA starts SoftDevice.

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
| `get pwrmgt.source`     | Returns composite source and confidence, e.g. `vusb+bat:valid`        |
| `get pwrmgt.bootreason` | Returns reset and shutdown reason strings                             |
| `get pwrmgt.bootmv`     | Returns boot voltage in millivolts, with `invalid` when BAT sense is not trustworthy |
| `get diag.boot`         | Returns current and previous reset-retained boot/radio diagnostics     |

On boards without power management enabled, all commands except `get pwrmgt.support` return:
```
ERROR: Power management not supported
```

`get diag.boot` includes raw current and previous boot records. The shutdown fields include all `GPREGRET2` markers, not only radio initialisation failure, so low-voltage and boot-protection context is preserved when diagnosing reset loops. The previous slot is retained across MCU resets where RAM is preserved; it is not a flash-backed history and may be lost after a complete power loss.

## Debug Output

When `MESH_DEBUG=1` is enabled, the power management module outputs:
```
DEBUG: PWRMGT: Reset = Wake from LPCOMP (0x20000); Shutdown = Low Voltage (0x4C)
DEBUG: PWRMGT: Boot voltage = 3450 mV (threshold = 3300 mV)
DEBUG: PWRMGT: LPCOMP wake configured (AIN7, ref=3/8 VDD)
```

## Phase 2 (Planned)

- Runtime voltage monitoring
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
