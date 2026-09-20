# Inhero MR2

Purpose-built solar repeater board: RAK4630 (nRF52840 + SX1262), BQ25798
buck/boost charger with universal 3.6–24 V solar input and MPPT, INA228
coulomb counter, RV-3028 RTC, BME280 environment sensor, 45 × 40 mm,
CE-certified (RED 2014/53/EU).

Supports Li-ion, LiFePO4, LTO and Na-ion battery profiles, solar-input recovery
and RTC wakeup after low-voltage sleep. An optional installation altitude
(`set board.altitude <metres>`) enables QNH pressure telemetry from the BME280.

Build environments:

```bash
pio run -e Inhero_MR2_repeater
pio run -e Inhero_MR2_repeater_bridge_rs232
pio run -e Inhero_MR2_sensor
```

Full documentation — quick start, datasheet, battery chemistry guide, power
management, telemetry, CLI reference and FAQ — is maintained by the manufacturer:

- [English documentation](https://docs.inhero.de/en/mr2/)
- [Deutsche Dokumentation](https://docs.inhero.de/mr2/)
- [Documentation sources in the vendor fork](https://github.com/liekmarflow/MeshCore/tree/main/variants/inhero_mr2/docs)
