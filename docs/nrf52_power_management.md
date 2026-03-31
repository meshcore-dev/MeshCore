# nRF52 Power Management

## Overview

The nRF52 power management module protects batteries from over-discharge, prevents brownout-related flash corruption, and enables automatic voltage-based recovery. When enabled and configured, it checks battery voltage at boot and enters protective shutdown (SYSTEMOFF) if voltage is too low, then automatically wakes when the battery recovers or external power is connected.

## CLI Commands

| Command | Description |
|---------|-------------|
| `get pwrmgt.support` | Returns "supported" or "unsupported" |
| `get pwrmgt.source` | Returns current power source: "battery" or "external" |
| `get pwrmgt.bootreason` | Returns reset reason and shutdown reason strings |
| `get pwrmgt.bootmv` | Returns battery voltage at boot in millivolts |
| `get pwrmgt.batt` | Returns configured battery chemistry: liion, lfp, or lto |
| `set pwrmgt.batt liion\|lfp\|lto` | Set battery chemistry (persisted, takes effect on next boot) |
| `get pwrmgt.bootlock` | Returns boot protection state: "enabled" or "disabled" |
| `set pwrmgt.bootlock on\|off` | Enable or disable boot protection (persisted, reboot required) |
| `poweroff` / `shutdown` | Immediate power off. Does not configure voltage wake — recovery requires USB power or a hardware wake source (e.g. button press). |

On boards without power management support, all commands except `get pwrmgt.support` return `ERROR: Power management not supported`.

## Configuration

### Set battery chemistry

The correct chemistry must be set so the firmware uses the right voltage thresholds. Default is Li-ion.

| Chemistry | Boot lockout threshold |
|-----------|----------------------|
| Li-ion/LiPo (`liion`) | 3000 mV |
| Lithium Iron Phosphate (`lfp`) | 2500 mV |
| Lithium Titanate Oxide (`lto`) | 1800 mV |

### Enable boot protection

Boot protection is disabled by default. Enable with `set pwrmgt.bootlock on`, then reboot.

Verify with `get pwrmgt.batt` and `get pwrmgt.bootlock`.

## Supported Boards

| Board | Implemented | LPCOMP wake | VBUS wake |
|-------|-------------|-------------|-----------|
| Seeed Studio XIAO nRF52840 (`xiao_nrf52`) | Yes | Yes | Yes |
| RAK4631 (`rak4631`) | Yes | Yes | Yes |
| Heltec T114 (`heltec_t114`) | Yes | Yes | Yes |
| SenseCAP Solar (`sensecap_solar`) | Yes | Yes | Yes |
| Promicro nRF52840 | No | No | No |
| RAK WisMesh Tag | No | No | No |
| Heltec Mesh Solar | No | No | No |
| LilyGo T-Echo / T-Echo Lite | No | No | No |
| WIO Tracker L1 / L1 E-Ink | No | No | No |
| WIO WM1110 | No | No | No |
| Mesh Pocket | No | No | No |
| Nano G2 Ultra | No | No | No |
| ThinkNode M1/M3/M6 | No | No | No |
| T1000-E | No | No | No |
| Ikoka Nano/Stick/Handheld (nRF) | No | No | No |
| Keepteen LT1 | No | No | No |
| Minewsemi ME25LS01 | No | No | No |

## How It Works

### Boot Voltage Protection

On every boot (when enabled), the firmware reads the battery voltage before starting mesh operations. If USB/external power is detected, the check is skipped. If the voltage is below the lockout threshold for the configured chemistry, the device configures LPCOMP and VBUS wake sources and enters SYSTEMOFF.

### Wake Sources

A device in protective shutdown can be woken by two sources:

- **LPCOMP (Low Power Comparator)**: The nRF52's hardware comparator monitors battery voltage on an analog input pin during SYSTEMOFF. When voltage rises above the configured recovery threshold, the comparator triggers a wake event. The recovery threshold is set above the boot lockout threshold so the device does not immediately shut down again. LPCOMP draws negligible current (~1 uA) during SYSTEMOFF.
- **VBUS (USB power detection)**: The nRF52's POWER peripheral detects USB voltage on the VBUS pin and triggers a wake event. This is configured alongside LPCOMP whenever the board routes VBUS to the nRF52 (standard on nRF52840 boards with native USB). Connecting any USB power source will wake the device immediately regardless of battery state.

### Shutdown Reasons

The firmware records why the device entered SYSTEMOFF. This value is stored in the GPREGRET2 register (persists across SYSTEMOFF) and can be queried after boot with `get pwrmgt.bootreason`.

| Code | Name | Description |
|------|------|-------------|
| 0x00 | NONE | Normal boot / no previous shutdown |
| 0x4C | LOW_VOLTAGE | Runtime low voltage threshold reached |
| 0x55 | USER | Manual shutdown via `poweroff` or `shutdown` CLI command |
| 0x42 | BOOT_PROTECT | Boot voltage protection triggered |

### Boot Reason Tracking

The firmware captures the nRF52 RESETREAS and GPREGRET2 registers at early boot before system initialisation clears them. This allows `get pwrmgt.bootreason` to report both the wake source (e.g. LPCOMP, VBUS, reset pin, watchdog) and the prior shutdown reason.

## LPCOMP Wake Voltage Reference

The LPCOMP wake voltage depends on the board's voltage divider ratio and the REFSEL value programmed before entering SYSTEMOFF.

**Wake voltage formula**:
```
VBAT_wake = REFSEL_fraction x VDD_sys x ADC_MULTIPLIER
```

Where VDD_sys is approximately 3.0-3.3V (regulator output during SYSTEMOFF) and ADC_MULTIPLIER is the board's voltage divider scale factor.

**REFSEL fraction reference**:

| REFSEL | Fraction | REFSEL | Fraction |
|--------|----------|--------|----------|
| 0 | 1/8 | 8 | 1/16 |
| 1 | 2/8 | 9 | 3/16 |
| 2 | 3/8 | 10 | 5/16 |
| 3 | 4/8 | 11 | 7/16 |
| 4 | 5/8 | 12 | 9/16 |
| 5 | 6/8 | 13 | 11/16 |
| 6 | 7/8 | 14 | 13/16 |
| 7 | ARef | 15 | 15/16 |

**Per-board per-chemistry wake voltages**:

| Board | ADC_MUL | Li-ion REFSEL | Wake range | LFP REFSEL | Wake range | LTO REFSEL | Wake range |
|-------|---------|---------------|------------|------------|------------|------------|------------|
| RAK4631 | ~1.73 | 4 (5/8) | 3.24-3.57V | 4 (5/8) | 3.24-3.57V | 11 (7/16) | 2.27-2.50V |
| T114 | 4.90 | 1 (2/8) | 3.68-4.04V | 9 (3/16) | 2.76-3.03V | 0 (1/8) | 1.84-2.02V* |
| XIAO | 3.0 | 2 (3/8) | 3.38-3.71V | 10 (5/16) | 2.81-3.09V | 1 (2/8) | 2.25-2.47V |

*T114 LTO uses a narrower margin (~50 mV plus LPCOMP 50 mV hysteresis) and assumes VDD_sys >= 3.0V in SYSTEMOFF.

## Debug Output

When the firmware is built with `MESH_DEBUG=1`, the power management module logs at boot:

```
PWRMGT: Reset = Wake from LPCOMP (0x20000); Shutdown = Low Voltage (0x4C)
PWRMGT: Boot protection enabled (Li-ion), threshold=3000 mV
PWRMGT: Boot voltage=3450 mV
PWRMGT: LPCOMP wake configured (AIN7, ref=3/8 VDD)
PWRMGT: VBUS wake configured
```

## References

- [nRF52840 Product Specification - POWER](https://infocenter.nordicsemi.com/topic/ps_nrf52840/power.html)
- [nRF52840 Product Specification - LPCOMP](https://infocenter.nordicsemi.com/topic/ps_nrf52840/lpcomp.html)
- [SoftDevice S140 API - Power Management](https://infocenter.nordicsemi.com/topic/sdk_nrf5_v17.1.0/group__nrf__sdm__api.html)
