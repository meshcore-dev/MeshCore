# Inhero MR2

Purpose-built solar repeater board: RAK4630 (nRF52840 + SX1262), BQ25798
buck/boost charger with universal 3.6–24 V solar input and MPPT, INA228
coulomb counter, RV-3028 RTC, BME280 environment sensor, 45 × 40 mm,
CE-certified (RED 2014/53/EU).

Build environments:

```bash
pio run -e Inhero_MR2_repeater
pio run -e Inhero_MR2_repeater_bridge_rs232
pio run -e Inhero_MR2_sensor
```

Full documentation (English and German) — quick start, datasheet, battery
chemistry guide, power management, telemetry, CLI reference, FAQ — is
maintained by the manufacturer at
<https://github.com/liekmarflow/MeshCore/tree/main/variants/inhero_mr2/docs>,
also reachable via <https://docs.inhero.de>.
