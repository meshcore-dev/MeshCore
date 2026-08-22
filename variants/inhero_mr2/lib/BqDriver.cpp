/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * BQ25798 Charger Driver Implementation
 */
#include "BqDriver.h"

#include <MeshCore.h>

BqDriver::BqDriver() {}

BqDriver::~BqDriver() {
  if (ih_i2c_dev) {
    delete ih_i2c_dev;
    ih_i2c_dev = nullptr;
  }
}

// Initializes BQ25798 charger and creates dedicated I2C device for NTC access
bool BqDriver::begin(uint8_t i2c_addr, TwoWire* wire) {
  if (!Adafruit_BQ25798::begin(i2c_addr, wire)) {
    // Cleanup any existing device before returning
    if (ih_i2c_dev) {
      delete ih_i2c_dev;
      ih_i2c_dev = nullptr;
    }
    return false;
  }
  if (ih_i2c_dev) {
    delete ih_i2c_dev;
  }
  ih_i2c_dev = new Adafruit_I2CDevice(i2c_addr, wire);
  if (!ih_i2c_dev->begin()) {
    // Cleanup on failure
    delete ih_i2c_dev;
    ih_i2c_dev = nullptr;
    return false;
  }
  return true;
}

// Reads Power Good status from charger — true if input power is sufficient for charging
bool BqDriver::getChargerStatusPowerGood() {
  Adafruit_BusIO_Register chrg_stat_0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_CHARGER_STATUS_0);
  Adafruit_BusIO_RegisterBits chrg_stat_0_bits = Adafruit_BusIO_RegisterBits(&chrg_stat_0_reg, 1, 3);

  uint8_t reg_value = chrg_stat_0_bits.read();

  return (bool)reg_value;
}

// Reads current charging state from charger
bq25798_charging_status BqDriver::getChargingStatus() {
  Adafruit_BusIO_Register chrg_stat_1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_CHARGER_STATUS_1);

  // Read with explicit error check. RegisterBits::read() would return bits of -1
  // on a failed I2C read — CHG_STAT[7:5] of 0xFF decodes as 0x07 = DONE_CHARGING,
  // which the SOC logic treats as "battery full".
  uint8_t reg_value = 0;
  if (!chrg_stat_1_reg.read(&reg_value, 1)) {
    return BQ25798_CHARGER_STATE_UNKNOWN;
  }

  return (bq25798_charging_status)((reg_value >> 5) & 0x07);
}

// Reads solar and temperature telemetry via BQ25798 ADC one-shot
//
// BQ25798 ADC Operating Conditions (Datasheet SLUSE22, Section 9.3.16):
//   "The ADC is allowed to operate if either VBUS > 3.4V or VBAT > 2.9V is valid.
//    At battery only condition, if the TS_ADC channel is enabled, the ADC only
//    works when battery voltage is higher than 3.2V, otherwise, the ADC works
//    when the battery voltage is higher than 2.9V."
//
// This means:
//   VBUS > 3.4V              → ADC runs, all channels available
//   VBAT >= 3.2V (no VBUS)   → ADC runs, all channels including TS
//   VBAT 2.9-3.2V (no VBUS)  → ADC runs ONLY if TS channel is DISABLED
//   VBAT < 2.9V (no VBUS)    → ADC cannot run at all
//
// Strategy:
//   1. If VBAT < 3.2V: disable TS channel to lower threshold to 2.9V
//      → Solar data (VBUS/IBUS) still readable, temperature returns N/A
//   2. If VBAT < 2.9V and no VBUS: ADC times out, all values zero/N/A
//   3. Only channels actually used on MR2 are enabled (IBUS, VBUS, TS)
//      — unused channels (IBAT, VBAT, VSYS, TDIE, D+, D-, VAC1, VAC2)
//      are disabled to prevent ADC_EN from hanging on unconnected pins.
//
// ADC_EN auto-clear behavior:
//   In one-shot mode, ADC_EN resets to 0 only when ALL enabled channels
//   have completed conversion. If any channel cannot complete (e.g. floating
//   input), ADC_EN stays 1 indefinitely. This is why unused channels MUST
//   be disabled via registers 0x2F/0x30.
//
// vbat_mv: battery voltage in mV from INA228 (0 = unknown, assume sufficient).
// Returns pointer to internal Telemetry struct (valid until next call).
const Telemetry* BqDriver::getTelemetryData(uint16_t vbat_mv) {
  telemetryData = { 0 };

  // Determine if TS channel can be enabled: chemistry must carry an NTC at all
  // (ts_ignore chemistries don't — RT2-only reads decode to a bogus ≈-46°C),
  // and VBAT must allow it (datasheet quote above: TS requires VBAT >= 3.2V
  // battery-only).
  bool ts_enabled = ntc_fitted;
  if (vbat_mv > 0 && vbat_mv < 3200) {
    ts_enabled = false;  // Disable TS → ADC threshold drops to 2.9V
  }

  bool success = this->startADCOneShot(ts_enabled);

  if (!success) {
    return &telemetryData;
  }

  // Poll ADC_EN bit until it auto-clears (conversion complete) or timeout.
  // Channels: IBUS + VBUS (+ TS if enabled) → ~48-72ms typical.
  const uint32_t ADC_TIMEOUT_MS = 250;
  uint32_t start = millis();
  bool conversion_done = false;
  while ((millis() - start) < ADC_TIMEOUT_MS) {
    if (!this->getADCEnabled()) {
      conversion_done = true;
      break;
    }
    delay(10);
  }

  if (!conversion_done) {
    this->setADCEnabled(false);
  }

  if (conversion_done) {
    telemetryData.solar.voltage = getVBUS();
    telemetryData.solar.current = getIBUS();
    if (telemetryData.solar.current < 0) {
      telemetryData.solar.current = 0;
    }
    telemetryData.solar.power = ((int32_t)telemetryData.solar.voltage * telemetryData.solar.current) / 1000;

    if (ts_enabled) {
      telemetryData.battery.temperature = this->calculateBatteryTemp(getTS());
    } else {
      // TS channel off — either the chemistry carries no NTC (ts_ignore)
      // or VBAT is too low to run the channel. No reading either way.
      telemetryData.battery.temperature = -888.0f;
    }
  } else {
    // ADC didn't complete — VBAT < 2.9V and no VBUS, or I2C issue
    telemetryData.battery.temperature = -888.0f;
  }

  telemetryData.solar.mppt = getMPPTenable();

  return &telemetryData;
}

// Calculates battery temperature in °C using Steinhart-Hart equation.
// Uses coefficients derived from Murata NCP15XH103F03RC datasheet R-T table.
// Max error vs. datasheet: ±0.36°C over -40..+125°C range.
//
// Per BQ25798 datasheet Figure 9-12: REGN → RT1 → TS → (RT2||NTC) → GND
// ts_pct: voltage at TS pin in percentage of REGN (e.g., 70.5 for 70.5%).
//         Special input values: -1.0 = I2C error, -2.0 = ADC not ready/invalid.
// Returns temperature in °C, or error codes:
//   -999.0 = I2C communication error
//   -888.0 = ADC not ready (read 0 or 0xFFFF)
//    -99.0 = NTC open/disconnected (k > 0.99)
//     99.0 = NTC short circuit (k < 0.01)
float BqDriver::calculateBatteryTemp(float ts_pct) {
  // Check for I2C read error
  if (ts_pct == -1.0f) return -999.0f; // I2C error
  if (ts_pct == -2.0f) return -888.0f; // ADC not ready or invalid value
  
  // Convert TS percentage to ratio (0.0 to 1.0)
  // TS% = 100 × R_bottom / (R_top + R_bottom)
  // where R_bottom = RT2 || NTC
  float k = ts_pct / 100.0f;

  // Plausibility check
  if (k > 0.99f) return -99.0f; // NTC open/disconnected
  if (k < 0.01f) return 99.0f;  // NTC short circuit

  // Calculate total resistance of bottom network (RT2 || NTC)
  // From: k = R_bottom / (RT1 + R_bottom)
  // Rearranged: R_bottom = RT1 × k / (1 - k)
  float r_bottom_total = R_PULLUP * (k / (1.0f - k));

  // Extract NTC resistance from parallel combination with RT2
  // For parallel resistors: 1/R_total = 1/R_NTC + 1/RT2
  // Therefore: 1/R_NTC = 1/R_total - 1/RT2
  float g_total = 1.0f / r_bottom_total;
  float g_rt2 = 1.0f / R_PARALLEL;

  if (g_total <= g_rt2) {
    return -99.0f; // Invalid measurement
  }

  float r_ntc = 1.0f / (g_total - g_rt2);

  // Apply Steinhart-Hart equation: 1/T = A + B·ln(R) + C·(ln(R))³
  float ln_r = logf(r_ntc);
  float inv_T = SH_A + SH_B * ln_r + SH_C * ln_r * ln_r * ln_r;

  // Convert Kelvin to Celsius
  return (1.0f / inv_T) - 273.15f;
}

// Getter/Setter for NTC Control 0 (0x17)
// Gets JEITA voltage setting for warm/cool regions
bq25798_jeita_vset_t BqDriver::getJeitaVSet() {
  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_vset_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 3, 5);

  uint8_t reg_value = jeita_vset_bits.read();

  return (bq25798_jeita_vset_t)reg_value;
}

// Sets JEITA voltage setting for warm/cool temperature regions
bool BqDriver::setJeitaVSet(bq25798_jeita_vset_t setting) {
  if (setting > BQ25798_JEITA_VSET_UNCHANGED) {
    return false;
  }

  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_vset_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 3, 5);

  jeita_vset_bits.write((uint8_t)setting);

  return true;
}

// Gets JEITA current setting for hot region
bq25798_jeita_iseth_t BqDriver::getJeitaISetH() {
  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_iseth_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 3);

  uint8_t reg_value = jeita_iseth_bits.read();

  return (bq25798_jeita_iseth_t)reg_value;
}

// Sets JEITA current setting for hot temperature region
bool BqDriver::setJeitaISetH(bq25798_jeita_iseth_t setting) {
  if (setting > BQ25798_JEITA_ISETH_UNCHANGED) {
    return false;
  }

  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_iseth_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 3);

  jeita_iseth_bits.write((uint8_t)setting);

  return true;
}

// Gets JEITA current setting for cold region
bq25798_jeita_isetc_t BqDriver::getJeitaISetC() {
  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_isetc_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 1);

  uint8_t reg_value = jeita_isetc_bits.read();

  return (bq25798_jeita_isetc_t)reg_value;
}

// Sets JEITA current setting for cold temperature region
bool BqDriver::setJeitaISetC(bq25798_jeita_isetc_t setting) {
  if (setting > BQ25798_JEITA_ISETC_UNCHANGED) {
    return false;
  }

  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_isetc_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 1);

  jeita_isetc_bits.write((uint8_t)setting);

  return true;
}

// Gets TS Cool threshold (lower boundary of COOL region)
bq25798_ts_cool_t BqDriver::getTsCool() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_cool_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 6);

  uint8_t reg_value = ts_cool_bits.read();

  return (bq25798_ts_cool_t)reg_value;
}

// Sets TS Cool threshold (lower boundary of COOL region)
bool BqDriver::setTsCool(bq25798_ts_cool_t threshold) {
  if (threshold > BQ25798_TS_COOL_20C) {
    return false;
  }

  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_cool_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 6);

  ts_cool_bits.write((uint8_t)threshold);

  return true;
}

// Gets TS Warm threshold (upper boundary of WARM region)
bq25798_ts_warm_t BqDriver::getTsWarm() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_warm_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 4);

  uint8_t reg_value = ts_warm_bits.read();

  return (bq25798_ts_warm_t)reg_value;
}

// Sets TS Warm threshold (upper boundary of WARM region)
bool BqDriver::setTsWarm(bq25798_ts_warm_t threshold) {
  if (threshold > BQ25798_TS_WARM_55C) {
    return false;
  }

  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_warm_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 4);

  ts_warm_bits.write((uint8_t)threshold);

  return true;
}

// Gets BHOT threshold (upper limit for charging)
bq25798_bhot_t BqDriver::getBHot() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bhot_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 2);

  uint8_t reg_value = bhot_bits.read();

  return (bq25798_bhot_t)reg_value;
}

// Sets BHOT threshold (upper limit for charging)
bool BqDriver::setBHot(bq25798_bhot_t threshold) {
  if (threshold > BQ25798_BHOT_DISABLE) {
    return false;
  }

  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bhot_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 2);

  bhot_bits.write((uint8_t)threshold);

  return true;
}

// Gets BCOLD threshold (lower limit for charging)
bq25798_bcold_t BqDriver::getBCold() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bcold_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 1);

  uint8_t reg_value = bcold_bits.read();

  return (bq25798_bcold_t)reg_value;
}

// Sets BCOLD threshold (lower limit for charging)
bool BqDriver::setBCold(bq25798_bcold_t threshold) {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bcold_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 1);

  bcold_bits.write((uint8_t)threshold);

  return true;
}

// Gets TS ignore status (disables all temperature monitoring)
bool BqDriver::getTsIgnore() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_ignore_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 0);

  return (bool)ts_ignore_bits.read();
}

// Sets TS ignore status (disables all temperature monitoring)
bool BqDriver::setTsIgnore(bool ignore) {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_ignore_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 0);

  ts_ignore_bits.write((uint8_t)ignore);

  return true;
}

// Starts ADC one-shot conversion for selected channels
//
// MR2 ADC Channel Map:
//   Reg 0x2F (ADC_FUNCTION_DISABLE_0): bit=1 means DISABLED
//     Bit 7: IBUS  → ENABLED  (solar current)
//     Bit 6: IBAT  → disabled (INA228 measures battery current)
//     Bit 5: VBUS  → ENABLED  (solar voltage)
//     Bit 4: VBAT  → disabled (INA228 measures battery voltage)
//     Bit 3: VSYS  → disabled (not used)
//     Bit 2: TS    → ENABLED or disabled depending on VBAT level
//     Bit 1: TDIE  → disabled (not used)
//     Bit 0: reserved
//
//   Reg 0x30 (ADC_FUNCTION_DISABLE_1): all disabled on MR2
//     Bit 7: D+   → disabled (AutoDPinsDetection=false, pin not connected)
//     Bit 6: D-   → disabled (pin not connected)
//     Bit 5: VAC2 → disabled (not routed on PCB)
//     Bit 4: VAC1 → disabled (not routed on PCB)
//
// Why only needed channels: ADC_EN only auto-clears when ALL enabled channels
// complete. Enabling unconnected channels (D+, D-, VAC) causes ADC_EN to hang
// indefinitely, requiring a timeout and forced disable.
//
// ts_enabled: true = enable TS channel (requires VBAT >= 3.2V per datasheet).
// Returns true if the I2C writes succeeded.
bool BqDriver::startADCOneShot(bool ts_enabled) {
  Adafruit_BusIO_Register disable_reg_0 = Adafruit_BusIO_Register(ih_i2c_dev, 0x2F);
  Adafruit_BusIO_Register disable_reg_1 = Adafruit_BusIO_Register(ih_i2c_dev, 0x30);

  // Reg 0x2F bit map: IBUS(7) IBAT(6) VBUS(5) VBAT(4) VSYS(3) TS(2) TDIE(1) reserved(0)
  // 1 = disabled, 0 = enabled
  uint8_t disable0 = 0x58;  // Enable IBUS(7), VBUS(5), TS(2), TDIE(1) — disable rest
  if (!ts_enabled) {
    disable0 |= 0x04;       // Also disable TS(2) → 0x5C
  }
  if (!disable_reg_0.write(disable0)) { return false; }

  // Reg 0x30: Disable all — D+(7), D-(6), VAC2(5), VAC1(4) not connected on MR2
  if (!disable_reg_1.write(0xF0)) { return false; }

  Adafruit_BusIO_Register adc_ctrl_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_ADC_CONTROL);
  bool ok = adc_ctrl_reg.write(0xC0);
  return ok;
}

// ADC Control register (0x2E) implementations
bool BqDriver::getADCEnabled() {
  Adafruit_BusIO_Register adc_ctrl_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_ADC_CONTROL);
  Adafruit_BusIO_RegisterBits adc_en_bits = Adafruit_BusIO_RegisterBits(&adc_ctrl_reg, 1, 7);
  bool result = (bool)adc_en_bits.read();
  return result;
}

bool BqDriver::setADCEnabled(bool enabled) {
  Adafruit_BusIO_Register adc_ctrl_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_ADC_CONTROL);
  Adafruit_BusIO_RegisterBits adc_en_bits = Adafruit_BusIO_RegisterBits(&adc_ctrl_reg, 1, 7);
  bool ok = adc_en_bits.write((uint8_t)enabled);
  return ok;
}

// ADC Reading implementations
int16_t BqDriver::getIBUS() {
  Adafruit_BusIO_Register ibus_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_IBUS_ADC, 2, MSBFIRST);
  uint16_t raw;
  if (!ibus_reg.read(&raw)) { // MSB first
    return 0;
  }
  int16_t val = (int16_t)raw; // 2's complement for signed
  return val;                 // in mA
}

uint16_t BqDriver::getVBUS() {
  Adafruit_BusIO_Register vbus_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_VBUS_ADC, 2, MSBFIRST);
  uint16_t val;
  if (!vbus_reg.read(&val)) {
    return 0;
  }
  return val; // in mV
}

float BqDriver::getDieTemperature_C() {
  Adafruit_BusIO_Register tdie_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_TDIE_ADC, 2, MSBFIRST);
  uint16_t raw;
  if (!tdie_reg.read(&raw)) {
    return -999.0f;
  }
  return (int16_t)raw * 0.5f; // 2's complement, 0.5°C/LSB
}

float BqDriver::getTS() {
  Adafruit_BusIO_Register ts_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_TS_ADC, 2, MSBFIRST);
  uint16_t val;
  
  // Try up to 3 times with small delays if we get invalid values
  for (int retry = 0; retry < 3; retry++) {
    if (!ts_reg.read(&val)) {
      delay(20);
      continue; // I2C read error, retry
    }
    // Check for invalid/uninitialized ADC value (0 or 0xFFFF)
    if (val == 0 || val == 0xFFFF) {
      if (retry < 2) {
        delay(50); // Wait a bit longer for ADC to settle
        continue;
      }
      return -2.0f; // ADC not ready / invalid value after retries
    }
    // Valid value
    return val * 0.09765625f; // 0.09765625 %/LSB (exact: 1/1024)
  }
  
  return -1.0f; // I2C read error after all retries
}

bool BqDriver::setVOCpercent(bq25798_voc_pct_t pct) {
  uint8_t reg15 = readReg(0x15);
  reg15 = (reg15 & 0x1F) | ((uint8_t)pct << 5);  // Bits [7:5] = VOC_PCT
  return writeReg(0x15, reg15);
}

bq25798_voc_pct_t BqDriver::getVOCpercent() {
  uint8_t reg15 = readReg(0x15);
  return (bq25798_voc_pct_t)((reg15 >> 5) & 0x07);
}


// Gets EN_AUTO_IBATDIS state (auto battery discharge during VBAT_OVP; POR default = enabled)
bool BqDriver::getAutoIBATDIS() {
  Adafruit_BusIO_Register ctrl0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_CHARGER_CONTROL_0);
  Adafruit_BusIO_RegisterBits auto_ibatdis_bit = Adafruit_BusIO_RegisterBits(&ctrl0_reg, 1, 7);
  return (bool)auto_ibatdis_bit.read();
}

// Sets EN_AUTO_IBATDIS (auto battery discharge during VBAT_OVP).
// enable: true = BQ sinks 30mA from BAT during OVP, false = no active discharge.
bool BqDriver::setAutoIBATDIS(bool enable) {
  Adafruit_BusIO_Register ctrl0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_CHARGER_CONTROL_0);
  Adafruit_BusIO_RegisterBits auto_ibatdis_bit = Adafruit_BusIO_RegisterBits(&ctrl0_reg, 1, 7);
  return auto_ibatdis_bit.write(enable ? 1 : 0);
}

// Non-static register access methods (use instance I2C config)
bool BqDriver::writeReg(uint8_t reg, uint8_t val) {
  if (!ih_i2c_dev) return false;
  
  uint8_t buffer[2] = {reg, val};
  bool ok = ih_i2c_dev->write(buffer, 2);
  return ok;
}

uint8_t BqDriver::readReg(uint8_t reg) {
  if (!ih_i2c_dev) return 0;
  
  uint8_t buffer[1] = {reg};
  if (!ih_i2c_dev->write_then_read(buffer, 1, buffer, 1)) {
    return 0;
  }
  return buffer[0];
}

// Static, raw-Wire helpers — safe pre-begin().
void BqDriver::maskAllInterrupts(TwoWire& wire, uint8_t addr) {
  static const uint8_t mask_regs[] = {0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D};
  for (uint8_t r : mask_regs) {
    wire.beginTransmission(addr);
    wire.write(r);
    wire.write(0xFF);
    wire.endTransmission();
  }
}

void BqDriver::clearInterruptFlags(TwoWire& wire, uint8_t addr) {
  static const uint8_t flag_regs[] = {0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
  for (uint8_t r : flag_regs) {
    wire.beginTransmission(addr);
    wire.write(r);
    wire.endTransmission(false);
    wire.requestFrom(addr, (uint8_t)1);
    while (wire.available()) wire.read();
  }
}

void BqDriver::disableAdc(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  wire.write(0x2E);  // ADC_CONTROL
  wire.write(0x00);
  wire.endTransmission();
}
