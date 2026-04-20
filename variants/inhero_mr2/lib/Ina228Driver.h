/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * INA228 Power Monitor Driver for Inhero MR-2
 * 
 * Features:
 * - Voltage, current, power monitoring
 * - Coulomb counter (accumulated charge)
 * - Alert pin for firmware-triggered low-voltage sleep (INA228 BUVL → ISR → System Sleep)
 * - Chemistry-specific thresholds (Li-Ion, LiFePO4, LTO)
 */

#pragma once

#include <Wire.h>
#include <Arduino.h>

// INA228 I2C Address
#define INA228_I2C_ADDR_DEFAULT 0x40  // A0=GND, A1=GND

// INA228 Register Map
#define INA228_REG_CONFIG       0x00  // Configuration
#define INA228_REG_ADC_CONFIG   0x01  // ADC Configuration
#define INA228_REG_SHUNT_CAL    0x02  // Shunt Calibration
#define INA228_REG_SHUNT_TEMP   0x03  // Shunt Temperature Coefficient
#define INA228_REG_VSHUNT       0x04  // Shunt Voltage
#define INA228_REG_VBUS         0x05  // Bus Voltage
#define INA228_REG_DIETEMP      0x06  // Die Temperature
#define INA228_REG_CURRENT      0x07  // Current
#define INA228_REG_POWER        0x08  // Power
#define INA228_REG_ENERGY       0x09  // Energy (Coulomb Counter)
#define INA228_REG_CHARGE       0x0A  // Charge (Coulomb Counter)
#define INA228_REG_DIAG_ALRT    0x0B  // Diagnostic and Alert
#define INA228_REG_SOVL         0x0C  // Shunt Over-Voltage Limit
#define INA228_REG_SUVL         0x0D  // Shunt Under-Voltage Limit
#define INA228_REG_BOVL         0x0E  // Bus Over-Voltage Limit
#define INA228_REG_BUVL         0x0F  // Bus Under-Voltage Limit
#define INA228_REG_TEMP_LIMIT   0x10  // Temperature Limit
#define INA228_REG_PWR_LIMIT    0x11  // Power Limit
#define INA228_REG_MANUFACTURER 0x3E  // Manufacturer ID (should be 0x5449 = "TI")
#define INA228_REG_DEVICE_ID    0x3F  // Device ID (should be 0x228)

// INA228 Configuration bits
#define INA228_CONFIG_RST       (1 << 15)  // Reset bit
#define INA228_CONFIG_ADCRANGE  (1 << 4)   // ADC Range (0=±163.84mV, 1=±40.96mV)

// ADC Configuration - Mode
#define INA228_ADC_MODE_CONT_ALL  0x0F  // Continuous conversion, all channels

// ADC Configuration - Averaging
#define INA228_ADC_AVG_1          0x00  // No averaging
#define INA228_ADC_AVG_4          0x01  // 4 samples average
#define INA228_ADC_AVG_16         0x02  // 16 samples average
#define INA228_ADC_AVG_64         0x03  // 64 samples average
#define INA228_ADC_AVG_128        0x04  // 128 samples average
#define INA228_ADC_AVG_256        0x05  // 256 samples average
#define INA228_ADC_AVG_512        0x06  // 512 samples average
#define INA228_ADC_AVG_1024       0x07  // 1024 samples average

// ADC Configuration - Conversion Time (VBUSCT, VSHCT, VTCT)
#define INA228_ADC_CT_50us        0x00  // 50 µs
#define INA228_ADC_CT_84us        0x01  // 84 µs
#define INA228_ADC_CT_150us       0x02  // 150 µs
#define INA228_ADC_CT_280us       0x03  // 280 µs
#define INA228_ADC_CT_540us       0x04  // 540 µs
#define INA228_ADC_CT_1052us      0x05  // 1052 µs (default)
#define INA228_ADC_CT_2074us      0x06  // 2074 µs
#define INA228_ADC_CT_4120us      0x07  // 4120 µs (maximum accuracy)

// Alert Configuration
#define INA228_DIAG_ALRT_ALATCH   (1 << 15)  // Alert Latch Enable
#define INA228_DIAG_ALRT_CNVR     (1 << 14)  // Conversion Ready
#define INA228_DIAG_ALRT_SLOWALERT (1 << 13) // Slow Alert (for averaging)
#define INA228_DIAG_ALRT_APOL     (1 << 12)  // Alert Polarity (1=active high)
#define INA228_DIAG_ALRT_ENERGYOF (1 << 11)  // Energy Overflow
#define INA228_DIAG_ALRT_CHARGEOF (1 << 10)  // Charge Overflow
#define INA228_DIAG_ALRT_MATHOF   (1 << 9)   // Math Overflow
#define INA228_DIAG_ALRT_TMPOL    (1 << 7)   // Temperature Over-Limit
#define INA228_DIAG_ALRT_SHNTOL   (1 << 6)   // Shunt Over-Voltage
#define INA228_DIAG_ALRT_SHNTUL   (1 << 5)   // Shunt Under-Voltage
#define INA228_DIAG_ALRT_BUSOL    (1 << 4)   // Bus Over-Voltage
#define INA228_DIAG_ALRT_BUSUL    (1 << 3)   // Bus Under-Voltage (UVLO)
#define INA228_DIAG_ALRT_POL      (1 << 2)   // Power Over-Limit
#define INA228_DIAG_ALRT_CNVRF    (1 << 1)   // Conversion Ready Flag
#define INA228_DIAG_ALRT_MEMSTAT  (1 << 0)   // Memory Status

/// Battery telemetry from INA228
typedef struct {
  uint16_t voltage_mv;       ///< Battery voltage in mV
  int16_t current_ma;        ///< Battery current in mA (+ = charging, - = discharging)
  int32_t power_mw;          ///< Battery power in mW
  int32_t energy_mwh;        ///< Accumulated energy in mWh (since last reset)
  float charge_mah;          ///< Accumulated charge in mAh (since last reset)
  float die_temp_c;          ///< Die temperature in °C
} Ina228BatteryData;

class Ina228Driver {
public:
  /// @brief Constructor
  /// @param i2c_addr I2C address of INA228 (default INA228_I2C_ADDR_DEFAULT for A0=A1=GND)
  Ina228Driver(uint8_t i2c_addr = INA228_I2C_ADDR_DEFAULT);

  /// @brief Initialize INA228 with default configuration
  /// @param shunt_resistor_mohm Shunt resistor value in milliohms (e.g., 10 for 0.01Ω)
  /// @return true if initialization successful
  bool begin(float shunt_resistor_mohm = 10.0f);

  /// @brief Check if INA228 is present and responsive
  /// @return true if device found
  bool isConnected();

  /// @brief Reset INA228 to default values
  void reset();

  /// @brief Read battery voltage
  /// @return Voltage in millivolts
  uint16_t readVoltage_mV();

  /// @brief Read battery current
  /// @return Current in milliamps (+ = charging, - = discharging)
  int16_t readCurrent_mA();

  /// @brief Read battery current with full precision
  /// @return Current in milliamps with decimal precision (+ = charging, - = discharging)
  /// @note Returns float for high-precision measurements (±1 LSB ≈ 1.91 µA)
  float readCurrent_mA_precise();

  /// @brief Read battery power
  /// @return Power in milliwatts
  int32_t readPower_mW();

  /// @brief Read accumulated energy (Coulomb Counter)
  /// @return Energy in milliwatt-hours
  int32_t readEnergy_mWh();

  /// @brief Read accumulated charge (Coulomb Counter)
  /// @return Charge in milliamp-hours
  float readCharge_mAh();

  /// @brief Read die temperature
  /// @return Temperature in °C
  float readDieTemperature_C();

  /// @brief Get all battery data in one call
  /// @param data Pointer to Ina228BatteryData struct
  /// @return true if read successful
  bool readAll(Ina228BatteryData* data);

  /// @brief Reset Coulomb Counter (energy and charge accumulators)
  void resetCoulombCounter();

  /// @brief Read back SHUNT_CAL register value (diagnostic)
  uint16_t readShuntCalRegister();

  /// @brief Read back ADC_CONFIG register value (diagnostic)
  uint16_t readAdcConfigRegister();

  /// @brief Read back CONFIG register value (diagnostic)
  uint16_t readConfigRegister();

  /// @brief Validate SHUNT_CAL and repair if corrupted
  /// @return true if SHUNT_CAL is correct (or was repaired), false if repair failed
  bool validateAndRepairShuntCal();

  /// @brief Set bus under-voltage alert threshold (for UVLO)
  /// @param voltage_mv Threshold in millivolts (e.g., 3200 for Li-Ion)
  /// @return true if successful
  bool setUnderVoltageAlert(uint16_t voltage_mv);

  /// @brief Set bus over-voltage alert threshold
  /// @param voltage_mv Threshold in millivolts
  /// @return true if successful
  bool setOverVoltageAlert(uint16_t voltage_mv);

  /// @brief Enable alert output on ALERT pin
  /// @param enable_uvlo Enable under-voltage alert
  /// @param enable_ovlo Enable over-voltage alert
  /// @param active_high True for active-high, false for active-low
  void enableAlert(bool enable_uvlo = true, bool active_high = false, bool latch_alert = false);

  /// @brief Check if alert condition is active
  /// @return true if alert flag is set
  bool isAlertActive();

  /// @brief Clear alert flags
  void clearAlert();

  /// @brief Get diagnostic and alert register value
  /// @return 16-bit diagnostic register value
  /// @warning In LATCH mode, reading DIAG_ALRT clears latched alert flags and de-asserts ALERT pin!
  uint16_t getDiagnosticFlags();

  /// @brief Read back current BUVL threshold register value
  /// @return Raw BUVL register value (multiply by 3.125 for mV)
  uint16_t readBuvlRegister();

  /// @brief Put INA228 into shutdown mode (power-down)
  /// @note Disables all measurements and Coulomb Counter to save power
  /// @return true if shutdown confirmed via readback, false if write failed
  bool shutdown();

  /// @brief Wake INA228 from shutdown mode
  /// @note Re-enables continuous measurement mode
  void wakeup();

  /// @brief Calibrate current measurement based on actual measured current
  /// @param actual_current_ma Actual measured battery current in milliamps
  /// @return Calculated calibration factor (multiplier for future readings)
  /// @note This calculates the ratio between actual and measured current.
  ///       The calibration factor should be stored persistently and applied via setCalibrationFactor().
  float calibrateCurrent(float actual_current_ma);

  /// @brief Set persistent current calibration factor
  /// @param factor Calibration factor (1.0 = no correction, >1.0 = increase readings, <1.0 = decrease)
  /// @note This factor is written directly to the INA228 SHUNT_CAL register (hardware calibration).
  ///       All measurements (current, power, energy, charge) are automatically corrected.
  ///       Call this at startup with value loaded from persistent storage.
  void setCalibrationFactor(float factor);

  /// @brief Get current calibration factor
  /// @return Current calibration factor (1.0 = no calibration)
  float getCalibrationFactor() const;



  /// @brief Read battery voltage directly via I2C without requiring driver initialization
  /// @param wire Pointer to TwoWire instance
  /// @param i2c_addr I2C address (default INA228_I2C_ADDR_DEFAULT)
  /// @return Battery voltage in millivolts, or 0 if read fails
  /// @note Static method for early boot use before INA228 is initialized.
  ///       Triggers One-Shot ADC conversion for accurate voltage reading.
  ///       Uses high-precision 24-bit ADC (±0.1% accuracy).
  static uint16_t readVBATDirect(TwoWire* wire = &Wire, uint8_t i2c_addr = INA228_I2C_ADDR_DEFAULT);

private:
  uint8_t _i2c_addr;
  float _shunt_mohm;
  float _current_lsb;  // Current LSB in A (constant per datasheet)
  uint16_t _base_shunt_cal;  // Original SHUNT_CAL value (before calibration)
  float _calibration_factor;  // Current calibration factor (1.0 = no correction)

  /// @brief Write 16-bit register
  bool writeRegister16(uint8_t reg, uint16_t value);

  /// @brief Read 16-bit register
  uint16_t readRegister16(uint8_t reg);

  /// @brief Read 24-bit register (sign-extended to 32-bit)
  int32_t readRegister24(uint8_t reg);

  /// @brief Read 40-bit register (for energy/charge)
  int64_t readRegister40(uint8_t reg);
};
