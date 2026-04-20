/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * BQ25798 Charger Driver Implementation
 */
#include "BqDriver.h"

#include <MeshCore.h>

/// @brief Default constructor
BqDriver::BqDriver() {}

/// @brief Destructor - cleans up I2C device
BqDriver::~BqDriver() {
  if (ih_i2c_dev) {
    delete ih_i2c_dev;
    ih_i2c_dev = nullptr;
  }
}

/// @brief Initializes BQ25798 charger and creates dedicated I2C device for NTC access
/// @param i2c_addr I2C address of BQ25798 (default: 0x6B)
/// @param wire Pointer to TwoWire instance (default: &Wire)
/// @return true if initialization successful
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

/// @brief Reads Power Good status from charger
/// @return true if input power is good (sufficient for charging)
bool BqDriver::getChargerStatusPowerGood() {
  Adafruit_BusIO_Register chrg_stat_0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_CHARGER_STATUS_0);
  Adafruit_BusIO_RegisterBits chrg_stat_0_bits = Adafruit_BusIO_RegisterBits(&chrg_stat_0_reg, 1, 3);

  uint8_t reg_value = chrg_stat_0_bits.read();

  return (bool)reg_value;
}

/// @brief Reads current charging state from charger
/// @return Charging status enum (NOT_CHARGING, PRE_CHARGING, CC, CV, etc.)
bq25798_charging_status BqDriver::getChargingStatus() {
  Adafruit_BusIO_Register chrg_stat_1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_CHARGER_STATUS_1);
  Adafruit_BusIO_RegisterBits chrg_stat_1_bits = Adafruit_BusIO_RegisterBits(&chrg_stat_1_reg, 3, 5);

  uint8_t reg_value = chrg_stat_1_bits.read();

  return (bq25798_charging_status)reg_value;
}

/// @brief Reads solar and temperature telemetry via BQ25798 ADC one-shot
///
/// BQ25798 ADC Operating Conditions (Datasheet SLUSE22, Section 9.3.16):
///   "The ADC is allowed to operate if either VBUS > 3.4V or VBAT > 2.9V is valid.
///    At battery only condition, if the TS_ADC channel is enabled, the ADC only
///    works when battery voltage is higher than 3.2V, otherwise, the ADC works
///    when the battery voltage is higher than 2.9V."
///
/// This means:
///   VBUS > 3.4V              â†’ ADC runs, all channels available
///   VBAT >= 3.2V (no VBUS)   â†’ ADC runs, all channels including TS
///   VBAT 2.9-3.2V (no VBUS)  â†’ ADC runs ONLY if TS channel is DISABLED
///   VBAT < 2.9V (no VBUS)    â†’ ADC cannot run at all
///
/// Strategy:
///   1. If VBAT < 3.2V: disable TS channel to lower threshold to 2.9V
///      â†’ Solar data (VBUS/IBUS) still readable, temperature returns N/A
///   2. If VBAT < 2.9V and no VBUS: ADC times out, all values zero/N/A
///   3. Only channels actually used on MR2 are enabled (IBUS, VBUS, TS)
///      â€” unused channels (IBAT, VBAT, VSYS, TDIE, D+, D-, VAC1, VAC2)
///      are disabled to prevent ADC_EN from hanging on unconnected pins.
///
/// ADC_EN auto-clear behavior:
///   In one-shot mode, ADC_EN resets to 0 only when ALL enabled channels
///   have completed conversion. If any channel cannot complete (e.g. floating
///   input), ADC_EN stays 1 indefinitely. This is why unused channels MUST
///   be disabled via registers 0x2F/0x30.
///
/// @param vbat_mv Battery voltage in mV from INA228 (0 = unknown, assume sufficient)
/// @return Pointer to internal Telemetry struct (valid until next call)
const Telemetry* const BqDriver::getTelemetryData(uint16_t vbat_mv) {
  telemetryData = { 0 };

  // Determine if TS channel can be enabled based on VBAT
  // See datasheet quote above: TS enabled requires VBAT >= 3.2V (battery-only)
  bool ts_enabled = true;
  if (vbat_mv > 0 && vbat_mv < 3200) {
    ts_enabled = false;  // Disable TS â†’ ADC threshold drops to 2.9V
  }

  bool success = this->startADCOneShot(ts_enabled);

  if (!success) {
    return &telemetryData;
  }

  // Poll ADC_EN bit until it auto-clears (conversion complete) or timeout.
  // Channels: IBUS + VBUS (+ TS if enabled) â†’ ~48-72ms typical.
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
      telemetryData.batterie.temperature = this->calculateBatteryTemp(getTS());
    } else {
      // TS disabled due to low VBAT â€” cannot read NTC
      telemetryData.batterie.temperature = -888.0f;
    }
  } else {
    // ADC didn't complete â€” VBAT < 2.9V and no VBUS, or I2C issue
    telemetryData.batterie.temperature = -888.0f;
  }

  telemetryData.solar.mppt = getMPPTenable();

  return &telemetryData;
}

/**
 * Calculates battery temperature in Â°C using Steinhart-Hart equation.
 * Uses coefficients derived from Murata NCP15XH103F03RC datasheet R-T table.
 * Max error vs. datasheet: Â±0.36Â°C over -40..+125Â°C range.
 *
 * Per BQ25798 datasheet Figure 9-12: REGN â†’ RT1 â†’ TS â†’ (RT2||NTC) â†’ GND
 * @param ts_pct Voltage at TS pin in percentage of REGN (e.g., 70.5 for 70.5%)
 *               Special values: -1.0 = I2C error, -2.0 = ADC not ready/invalid
 * @return Temperature in Â°C, or error codes:
 *         -999.0 = I2C communication error
 *         -888.0 = ADC not ready (read 0 or 0xFFFF)
 *          -99.0 = NTC open/disconnected (k > 0.99)
 *           99.0 = NTC short circuit (k < 0.01)
 */
float BqDriver::calculateBatteryTemp(float ts_pct) {
  // Check for I2C read error
  if (ts_pct == -1.0f) return -999.0f; // I2C error
  if (ts_pct == -2.0f) return -888.0f; // ADC not ready or invalid value
  
  // Convert TS percentage to ratio (0.0 to 1.0)
  // TS% = 100 Ã— R_bottom / (R_top + R_bottom)
  // where R_bottom = RT2 || NTC
  float k = ts_pct / 100.0f;

  // Plausibility check
  if (k > 0.99f) return -99.0f; // NTC open/disconnected
  if (k < 0.01f) return 99.0f;  // NTC short circuit

  // Calculate total resistance of bottom network (RT2 || NTC)
  // From: k = R_bottom / (RT1 + R_bottom)
  // Rearranged: R_bottom = RT1 Ã— k / (1 - k)
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

  // Apply Steinhart-Hart equation: 1/T = A + BÂ·ln(R) + CÂ·(ln(R))Â³
  float ln_r = logf(r_ntc);
  float inv_T = SH_A + SH_B * ln_r + SH_C * ln_r * ln_r * ln_r;

  // Convert Kelvin to Celsius
  return (1.0f / inv_T) - 273.15f;
}

// Getter/Setter for NTC Control 0 (0x17)
/// @brief Gets JEITA voltage setting for warm/cool regions
/// @return JEITA VSET enum value
bq25798_jeita_vset_t BqDriver::getJeitaVSet() {
  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_vset_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 3, 5);

  uint8_t reg_value = jeita_vset_bits.read();

  return (bq25798_jeita_vset_t)reg_value;
}

/// @brief Sets JEITA voltage setting for warm/cool temperature regions
/// @param setting JEITA VSET enum (suspend, or VREG offset)
/// @return true if successful
bool BqDriver::setJeitaVSet(bq25798_jeita_vset_t setting) {
  if (setting > BQ25798_JEITA_VSET_UNCHANGED) {
    return false;
  }

  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_vset_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 3, 5);

  jeita_vset_bits.write((uint8_t)setting);

  return true;
}

/// @brief Gets JEITA current setting for hot region
/// @return JEITA ISETH enum value
bq25798_jeita_iseth_t BqDriver::getJeitaISetH() {
  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_iseth_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 3);

  uint8_t reg_value = jeita_iseth_bits.read();

  return (bq25798_jeita_iseth_t)reg_value;
}

/// @brief Sets JEITA current setting for hot temperature region
/// @param setting JEITA ISETH enum (suspend or percentage)
/// @return true if successful
bool BqDriver::setJeitaISetH(bq25798_jeita_iseth_t setting) {
  if (setting > BQ25798_JEITA_ISETH_UNCHANGED) {
    return false;
  }

  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_iseth_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 3);

  jeita_iseth_bits.write((uint8_t)setting);

  return true;
}

/// @brief Gets JEITA current setting for cold region
/// @return JEITA ISETC enum value
bq25798_jeita_isetc_t BqDriver::getJeitaISetC() {
  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_isetc_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 1);

  uint8_t reg_value = jeita_isetc_bits.read();

  return (bq25798_jeita_isetc_t)reg_value;
}

/// @brief Sets JEITA current setting for cold temperature region
/// @param setting JEITA ISETC enum (suspend or percentage)
/// @return true if successful
bool BqDriver::setJeitaISetC(bq25798_jeita_isetc_t setting) {
  if (setting > BQ25798_JEITA_ISETC_UNCHANGED) {
    return false;
  }

  Adafruit_BusIO_Register ntc0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_0);
  Adafruit_BusIO_RegisterBits jeita_isetc_bits = Adafruit_BusIO_RegisterBits(&ntc0_reg, 2, 1);

  jeita_isetc_bits.write((uint8_t)setting);

  return true;
}

/// @brief Gets TS Cool threshold (lower boundary of COOL region)
/// @return TS COOL enum value
bq25798_ts_cool_t BqDriver::getTsCool() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_cool_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 6);

  uint8_t reg_value = ts_cool_bits.read();

  return (bq25798_ts_cool_t)reg_value;
}

/// @brief Sets TS Cool threshold (lower boundary of COOL region)
/// @param threshold TS COOL enum (0Â°C to 20Â°C)
/// @return true if successful
bool BqDriver::setTsCool(bq25798_ts_cool_t threshold) {
  if (threshold > BQ25798_TS_COOL_20C) {
    return false;
  }

  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_cool_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 6);

  ts_cool_bits.write((uint8_t)threshold);

  return true;
}

/// @brief Gets TS Warm threshold (upper boundary of WARM region)
/// @return TS WARM enum value
bq25798_ts_warm_t BqDriver::getTsWarm() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_warm_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 4);

  uint8_t reg_value = ts_warm_bits.read();

  return (bq25798_ts_warm_t)reg_value;
}

/// @brief Sets TS Warm threshold (upper boundary of WARM region)
/// @param threshold TS WARM enum (40Â°C to 55Â°C)
/// @return true if successful
bool BqDriver::setTsWarm(bq25798_ts_warm_t threshold) {
  if (threshold > BQ25798_TS_WARM_55C) {
    return false;
  }

  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_warm_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 4);

  ts_warm_bits.write((uint8_t)threshold);

  return true;
}

/// @brief Gets BHOT threshold (upper limit for charging)
/// @return BHOT enum value
bq25798_bhot_t BqDriver::getBHot() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bhot_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 2);

  uint8_t reg_value = bhot_bits.read();

  return (bq25798_bhot_t)reg_value;
}

/// @brief Sets BHOT threshold (upper limit for charging)
/// @param threshold BHOT enum (55Â°C to 65Â°C, or disable)
/// @return true if successful
bool BqDriver::setBHot(bq25798_bhot_t threshold) {
  if (threshold > BQ25798_BHOT_DISABLE) {
    return false;
  }

  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bhot_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 2, 2);

  bhot_bits.write((uint8_t)threshold);

  return true;
}

/// @brief Gets BCOLD threshold (lower limit for charging)
/// @return BCOLD enum value
bq25798_bcold_t BqDriver::getBCold() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bcold_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 1);

  uint8_t reg_value = bcold_bits.read();

  return (bq25798_bcold_t)reg_value;
}

/// @brief Sets BCOLD threshold (lower limit for charging)
/// @param threshold BCOLD enum (-10Â°C or -20Â°C)
/// @return true if successful
bool BqDriver::setBCold(bq25798_bcold_t threshold) {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits bcold_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 1);

  bcold_bits.write((uint8_t)threshold);

  return true;
}

/// @brief Gets TS ignore status (disables all temperature monitoring)
/// @return true if temperature monitoring disabled
bool BqDriver::getTsIgnore() {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_ignore_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 0);

  return (bool)ts_ignore_bits.read();
}

/// @brief Sets TS ignore status (disables all temperature monitoring)
/// @param ignore true to ignore temperature monitoring
/// @return true if successful
bool BqDriver::setTsIgnore(bool ignore) {
  Adafruit_BusIO_Register ntc1_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_NTC_CONTROL_1);
  Adafruit_BusIO_RegisterBits ts_ignore_bits = Adafruit_BusIO_RegisterBits(&ntc1_reg, 1, 0);

  ts_ignore_bits.write((uint8_t)ignore);

  return true;
}

/// @brief Starts ADC one-shot conversion for selected channels
///
/// MR2 ADC Channel Map:
///   Reg 0x2F (ADC_FUNCTION_DISABLE_0): bit=1 means DISABLED
///     Bit 7: IBUS  â†’ ENABLED  (solar current)
///     Bit 6: IBAT  â†’ disabled (INA228 measures battery current)
///     Bit 5: VBUS  â†’ ENABLED  (solar voltage)
///     Bit 4: VBAT  â†’ disabled (INA228 measures battery voltage)
///     Bit 3: VSYS  â†’ disabled (not used)
///     Bit 2: TS    â†’ ENABLED or disabled depending on VBAT level
///     Bit 1: TDIE  â†’ disabled (not used)
///     Bit 0: reserved
///
///   Reg 0x30 (ADC_FUNCTION_DISABLE_1): all disabled on MR2
///     Bit 7: D+   â†’ disabled (AutoDPinsDetection=false, pin not connected)
///     Bit 6: D-   â†’ disabled (pin not connected)
///     Bit 5: VAC2 â†’ disabled (not routed on PCB)
///     Bit 4: VAC1 â†’ disabled (not routed on PCB)
///
/// Why only needed channels: ADC_EN only auto-clears when ALL enabled channels
/// complete. Enabling unconnected channels (D+, D-, VAC) causes ADC_EN to hang
/// indefinitely, requiring a timeout and forced disable.
///
/// @param ts_enabled true = enable TS channel (requires VBAT >= 3.2V per datasheet)
/// @return true if I2C writes successful
bool BqDriver::startADCOneShot(bool ts_enabled) {
  Adafruit_BusIO_Register disable_reg_0 = Adafruit_BusIO_Register(ih_i2c_dev, 0x2F);
  Adafruit_BusIO_Register disable_reg_1 = Adafruit_BusIO_Register(ih_i2c_dev, 0x30);

  // Reg 0x2F bit map: IBUS(7) IBAT(6) VBUS(5) VBAT(4) VSYS(3) TS(2) TDIE(1) reserved(0)
  // 1 = disabled, 0 = enabled
  uint8_t disable0 = 0x5A;  // Enable IBUS(7), VBUS(5), TS(2) â€” disable rest
  if (!ts_enabled) {
    disable0 |= 0x04;       // Also disable TS(2) â†’ 0x5E
  }
  if (!disable_reg_0.write(disable0)) { return false; }

  // Reg 0x30: Disable all â€” D+(7), D-(6), VAC2(5), VAC1(4) not connected on MR2
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


/// @brief Gets EN_AUTO_IBATDIS state (auto battery discharge during VBAT_OVP)
/// @return true if auto discharge is enabled (POR default = enabled)
bool BqDriver::getAutoIBATDIS() {
  Adafruit_BusIO_Register ctrl0_reg = Adafruit_BusIO_Register(ih_i2c_dev, BQ25798_REG_CHARGER_CONTROL_0);
  Adafruit_BusIO_RegisterBits auto_ibatdis_bit = Adafruit_BusIO_RegisterBits(&ctrl0_reg, 1, 7);
  return (bool)auto_ibatdis_bit.read();
}

/// @brief Sets EN_AUTO_IBATDIS (auto battery discharge during VBAT_OVP)
/// @param enable true = BQ sinks 30mA from BAT during OVP, false = no active discharge
/// @return true if successful
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
