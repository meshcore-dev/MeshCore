# Inhero MR-2

Smart solar mesh repeater board for autonomous long-term deployment.

**Hardware Revision:** 1.1

## Why another repeater board?

Existing solar-capable boards require the user to match panels, chargers, and fuel gauges manually — and none of them recover autonomously after a deep discharge. The Inhero MR-2 is a single-board solution (45 × 40 mm) that combines:

- **Universal MPPT solar input (3.6–24 V, buck/boost)** — any panel up to 25 V Voc, including boost charging (e.g. 5 V panel → 5.4 V 2S LTO).
- **4 battery chemistries, software-selectable** — 1S Li-Ion, 1S LiFePO₄, 2S LTO, 1S Na-Ion. No hardware change required.
- **Under-voltage sleep & autonomous recovery** — deep sleep < 500 µA with solar charging active; hourly RTC wake checks if restart is possible.
- **INA228 coulomb counting** — true SOC tracking as you know it from smartphones, instead of unreliable voltage-based guesswork.
- **On-board BME280 + RV-3028 RTC** — environment telemetry and stable time base even during hibernation.
- **6.0 mA @ 4.2 V** total system RX current. TPS62840 high-efficiency rail.
- **JEITA temperature zones** — automatic charge throttling in cold/hot conditions.
- **Hardware watchdog (600 s)** — a silent firmware freeze is impossible.
- **CE certification (RED 2014/53/EU) planned.**

## Key Specifications

| Parameter | Value |
|---|---|
| **Core Module** | RAK4630 (nRF52840 + SX1262) |
| **Charger** | TI BQ25798 (buck/boost, MPPT, JEITA) |
| **Power Monitor** | TI INA228 (coulomb counter, ALERT) |
| **RTC** | RV-3028-C7 (wake timer) |
| **Environment Sensor** | Bosch BME280 (T, H, P) |
| **Buck Converter** | TI TPS62840 (3.3 V, 750 mA) |
| **Solar Input** | 3.6–24 V (MPPT), max Voc 25 V |
| **Battery Chemistries** | 1S Li-Ion, 1S LiFePO4, 2S LTO, 1S Na-Ion |
| **Charge Current** | 50–1500 mA (configurable) |
| **Active Current** | 6.0 mA @ 4.2 V (USB off, no TX) |
| **Sleep Current** | < 500 µA (RTC wake, solar charging active) |
| **PCB Size** | 45 × 40 mm |

## I2C Address Map

| Address | Component |
|---------|-----------|
| 0x40 | INA228 |
| 0x52 | RV-3028-C7 |
| 0x6B | BQ25798 |
| 0x76 | BME280 |

## Key GPIOs

| GPIO | Function |
|------|----------|
| P0.04 (WB_IO4) | BQ CE pin (via N-FET, inverted) |
| P1.02 | INA228 ALERT (low-voltage interrupt) |
| GPIO17 (WB_IO1) | RV-3028 RTC interrupt |
| GPIO21 | BQ25798 INT |

## Build Targets

| Environment | Description |
|---|---|
| `Inhero_MR2_repeater` | Standard repeater |
| `Inhero_MR2_repeater_bridge_rs232` | Repeater with RS232 bridge |
| `Inhero_MR2_sensor` | Sensor firmware |

## Documentation

Full documentation (datasheet, battery guide, FAQ, CLI reference):
**[docs.inhero.de](https://docs.inhero.de)**
