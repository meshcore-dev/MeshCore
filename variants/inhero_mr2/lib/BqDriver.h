/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * BQ25798 Charger Driver for Inhero MR-2
 * Extends Adafruit_BQ25798 library (BSD License).
 */

#pragma once

#include <Adafruit_BQ25798.h>
#include <Wire.h>
#include <math.h>

#define R_PULLUP    5600.0f  ///< Upper resistor RT1 in Ohms
#define R_PARALLEL  27000.0f ///< Lower parallel resistor RT2 in Ohms

// Steinhart-Hart coefficients for NCP15XH103F03RC NTC (10kΩ, B=3380)
// Fitted from Murata datasheet R-T table at -20°C, 25°C, 85°C
// Max error vs. datasheet: ±0.36°C over -40..+125°C range
#define SH_A  8.7248136876e-04f  ///< Steinhart-Hart coefficient A
#define SH_B  2.5405556775e-04f  ///< Steinhart-Hart coefficient B
#define SH_C  1.8122847672e-07f  ///< Steinhart-Hart coefficient C

/// Solar input telemetry data
/// @note Solar current from BQ25798 IBUS ADC has significant error at low currents (~±30mA).
///       Values are approximate - treat as estimates, not precise measurements.
typedef struct {
  uint16_t voltage; ///< Solar voltage in mV
  int16_t current;  ///< Solar current in mA (approximate, BQ25798 IBUS ADC has ~±30mA error at low currents)
  int32_t power;    ///< Solar power in mW
  bool mppt;        ///< MPPT enabled status
} SolarData;

/// Battery telemetry data
typedef struct {
  uint16_t voltage; ///< Battery voltage in mV
  float current;    ///< Battery current in mA (positive = charging, negative = discharging)
  int32_t power;    ///< Battery power in mW
  float temperature; ///< Battery temperature in °C
} BattData;

/// System voltage telemetry
typedef struct {
  uint16_t voltage; ///< System voltage in mV
} SysData;

/// Main telemetry container aggregating all data sources
typedef struct {
  SysData system;     ///< System voltage data
  SolarData solar;    ///< Solar input data
  BattData batterie;  ///< Battery data
} Telemetry;

/// JEITA voltage setting for warm/cool regions (NTC Control 0 Register 0x17)
typedef enum {
  BQ25798_JEITA_VSET_SUSPEND = 0x00,     ///< Charge Suspend
  BQ25798_JEITA_VSET_MINUS_800MV = 0x01, ///< Set VREG to VREG-800mV
  BQ25798_JEITA_VSET_MINUS_600MV = 0x02, ///< Set VREG to VREG-600mV
  BQ25798_JEITA_VSET_MINUS_400MV = 0x03, ///< Set VREG to VREG-400mV (default)
  BQ25798_JEITA_VSET_MINUS_300MV = 0x04, ///< Set VREG to VREG-300mV
  BQ25798_JEITA_VSET_MINUS_200MV = 0x05, ///< Set VREG to VREG-200mV
  BQ25798_JEITA_VSET_MINUS_100MV = 0x06, ///< Set VREG to VREG-100mV
  BQ25798_JEITA_VSET_UNCHANGED = 0x07    ///< VREG unchanged
} bq25798_jeita_vset_t;

typedef enum {
  BQ25798_JEITA_ISETH_SUSPEND = 0x00,    ///< Charge Suspend
  BQ25798_JEITA_ISETH_20_PERCENT = 0x01, ///< Set ICHG to 20% * ICHG
  BQ25798_JEITA_ISETH_40_PERCENT = 0x02, ///< Set ICHG to 40% * ICHG
  BQ25798_JEITA_ISETH_UNCHANGED = 0x03   ///< ICHG unchanged (default)
} bq25798_jeita_iseth_t;

typedef enum {
  BQ25798_JEITA_ISETC_SUSPEND = 0x00,    ///< Charge Suspend
  BQ25798_JEITA_ISETC_20_PERCENT = 0x01, ///< Set ICHG to 20% * ICHG (default)
  BQ25798_JEITA_ISETC_40_PERCENT = 0x02, ///< Set ICHG to 40% * ICHG
  BQ25798_JEITA_ISETC_UNCHANGED = 0x03   ///< ICHG unchanged
} bq25798_jeita_isetc_t;

typedef enum {
  BQ25798_CHARGER_STATE_NOT_CHARGING = 0x00,
  BQ25798_CHARGER_STATE_TRICKLE_CHARGING = 0x01,
  BQ25798_CHARGER_STATE_PRE_CHARGING = 0x02,
  BQ25798_CHARGER_STATE_CC_CHARGING = 0x03,
  BQ25798_CHARGER_STATE_CV_CHARGING = 0x04,
  BQ25798_CHARGER_STATE_TOP_OF_TIMER_ACTIVE_CHARGING = 0x06,
  BQ25798_CHARGER_STATE_DONE_CHARGING = 0x07

} bq25798_charging_status;

/// New enums for NTC Control 1 (Register 0x18)
typedef enum {
  BQ25798_TS_COOL_5C = 0x00,  ///< 71.1% of REGN (5°C)
  BQ25798_TS_COOL_10C = 0x01, ///< 68.4% of REGN (10°C, default)
  BQ25798_TS_COOL_15C = 0x02, ///< 65.5% of REGN (15°C)
  BQ25798_TS_COOL_20C = 0x03  ///< 62.4% of REGN (20°C)
} bq25798_ts_cool_t;

typedef enum {
  BQ25798_TS_WARM_40C = 0x00, ///< 48.4% of REGN (40°C)
  BQ25798_TS_WARM_45C = 0x01, ///< 44.8% of REGN (45°C, default)
  BQ25798_TS_WARM_50C = 0x02, ///< 41.2% of REGN (50°C)
  BQ25798_TS_WARM_55C = 0x03  ///< 37.7% of REGN (55°C)
} bq25798_ts_warm_t;

/// BHOT threshold - upper temperature limit for charging
typedef enum {
  BQ25798_BHOT_55C = 0x00,    ///< 55°C
  BQ25798_BHOT_60C = 0x01,    ///< 60°C (default)
  BQ25798_BHOT_65C = 0x02,    ///< 65°C
  BQ25798_BHOT_DISABLE = 0x03 ///< Disable BHOT protection
} bq25798_bhot_t;

/// BCOLD threshold - lower temperature limit for charging
typedef enum {
  BQ25798_BCOLD_MINUS_10C = 0x00, ///< -10°C (default)
  BQ25798_BCOLD_MINUS_20C = 0x01  ///< -20°C
} bq25798_bcold_t;

/// ADC resolution setting
typedef enum {
  BQ25798_ADC_SAMPLE_15BIT = 0b00, ///< 15-bit resolution (default, ~24ms conversion)
  BQ25798_ADC_SAMPLE_14BIT = 0b01, ///< 14-bit resolution
  BQ25798_ADC_SAMPLE_13BIT = 0b10, ///< 13-bit resolution
  BQ25798_ADC_SAMPLE_12BIT = 0b11  ///< 12-bit resolution (not recommended)
} bq25798_adc_sample_t;

/**
 * @brief Extended BQ25798 driver with NTC support and comprehensive telemetry
 * 
 * Extends Adafruit_BQ25798 with:
 * - JEITA temperature control (VSET, ISETH, ISETC)
 * - NTC thermistor temperature calculation
 * - Complete ADC telemetry (solar, battery, system)
 * - One-shot ADC conversion management
 */
class BqDriver : public Adafruit_BQ25798 {
public:
  BqDriver();
  ~BqDriver();

  bool begin(uint8_t i2c_addr = BQ25798_DEFAULT_ADDR, TwoWire* wire = &Wire);

  // Direct pass-through to parent — all I2C runs in tick() context (no concurrent access)
  bool setHIZMode(bool enable)        { return Adafruit_BQ25798::setHIZMode(enable); }
  bool setChargeEnable(bool enable)   { return Adafruit_BQ25798::setChargeEnable(enable); }
  bool getChargeEnable()              { return Adafruit_BQ25798::getChargeEnable(); }
  bool getMPPTenable()                { return Adafruit_BQ25798::getMPPTenable(); }
  bool setMPPTenable(bool enable)     { return Adafruit_BQ25798::setMPPTenable(enable); }
  bool setStatPinEnable(bool enable)  { return Adafruit_BQ25798::setStatPinEnable(enable); }
  bool getStatPinEnable()             { return Adafruit_BQ25798::getStatPinEnable(); }

  bq25798_jeita_vset_t getJeitaVSet();
  bool setJeitaVSet(bq25798_jeita_vset_t setting);

  bq25798_jeita_iseth_t getJeitaISetH();
  bool setJeitaISetH(bq25798_jeita_iseth_t setting);

  bq25798_jeita_isetc_t getJeitaISetC();
  bool setJeitaISetC(bq25798_jeita_isetc_t setting);

  bq25798_ts_cool_t getTsCool();
  bool setTsCool(bq25798_ts_cool_t threshold);

  bq25798_ts_warm_t getTsWarm();
  bool setTsWarm(bq25798_ts_warm_t threshold);

  bq25798_bhot_t getBHot();
  bool setBHot(bq25798_bhot_t threshold);

  bq25798_bcold_t getBCold();
  bool setBCold(bq25798_bcold_t threshold);

  bool getTsIgnore();
  bool setTsIgnore(bool ignore);

  /// @brief Read solar + temperature telemetry via BQ25798 ADC
  /// @param vbat_mv Battery voltage from INA228 in mV. Used to decide if TS channel
  ///        can be enabled (requires VBAT >= 3.2V without VBUS, per datasheet 9.3.16).
  ///        Pass 0 if unknown (assumes sufficient voltage).
  const Telemetry* const getTelemetryData(uint16_t vbat_mv = 0);

  // Charger Status
  bool getChargerStatusPowerGood();
  bq25798_charging_status getChargingStatus();

  bool setVOCpercent(bq25798_voc_pct_t pct);
  bq25798_voc_pct_t getVOCpercent();

  bool getAutoIBATDIS();
  bool setAutoIBATDIS(bool enable);

  // Non-static register access methods (use instance I2C config)
  bool writeReg(uint8_t reg, uint8_t val);
  uint8_t readReg(uint8_t reg);

  // ADC status/result accessors
  bool getADCEnabled();
  uint16_t getVBUS();

protected:
  Adafruit_I2CDevice* ih_i2c_dev = nullptr; ///< Dedicated I2C device for NTC access

private:
  bool startADCOneShot(bool ts_enabled = true);
  bool setADCEnabled(bool enabled);
  int16_t getIBUS();
  float getTS();   // TS voltage in % of REGN

  float calculateBatteryTemp(float ts_pct);
  Telemetry telemetryData = { 0 };
};