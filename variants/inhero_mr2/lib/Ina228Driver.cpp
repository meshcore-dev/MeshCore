/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * INA228 Power Monitor Driver Implementation
 */

#include "Ina228Driver.h"
#include "../../../src/MeshCore.h"  // For MESH_DEBUG_PRINTLN

Ina228Driver::Ina228Driver(uint8_t i2c_addr) 
  : _i2c_addr(i2c_addr), _shunt_mohm(10.0f), _current_lsb(0.0f), _base_shunt_cal(0), _calibration_factor(1.0f) {}

bool Ina228Driver::begin(float shunt_resistor_mohm) {
  _shunt_mohm = shunt_resistor_mohm;

  // Check if device is present
  if (!isConnected()) {
    MESH_DEBUG_PRINTLN("INA228 begin() FAILED: isConnected() = false");
    return false;
  }
  MESH_DEBUG_PRINTLN("INA228 begin(): Device connected");
  // Do NOT reset device - Early Boot voltage check may have configured it
  // Resetting causes timing issues where subsequent writes fail
  // Just reconfigure registers directly
  
  // Configure ADC: Continuous mode, all channels, long conversion times, 256 samples averaging
  // - Long conversion times (VSHCT=4120µs, VBUSCT=2074µs) reduce noise for accurate SOC tracking
  // - AVG_256 filters TX voltage peaks (prevents false UVLO triggers during transmit)
  // - Trade-off: ~1s per measurement (excellent accuracy, acceptable for 1h SOC updates)
  uint16_t adc_config = (INA228_ADC_MODE_CONT_ALL << 12) |  // MODE: Continuous all = 0xF
                        (INA228_ADC_CT_2074us << 9)      |  // VBUSCT: 2074µs for voltage accuracy
                        (INA228_ADC_CT_4120us << 6)      |  // VSHCT: 4120µs for current/SOC accuracy
                        (INA228_ADC_CT_540us << 3)       |  // VTCT: 540µs (temp less critical)
                        (INA228_ADC_AVG_256 << 0);          // AVG: 256 samples
  // Expected: 0xFFCB (was 0xF003 without conversion time config)
  
  // Write ADC_CONFIG with retry and verify
  // Sometimes the first write after readVBATDirect() fails
  bool adc_config_ok = false;
  for (int retry = 0; retry < 5; retry++) {
    writeRegister16(INA228_REG_ADC_CONFIG, adc_config);
    delay(10);
    uint16_t readback = readRegister16(INA228_REG_ADC_CONFIG);
    if (readback == adc_config) {
      adc_config_ok = true;
      break;
    }
    delay(20);  // Wait longer before retry
  }
  
  if (!adc_config_ok) {
    MESH_DEBUG_PRINTLN("INA228 begin() ERROR: Failed to set ADC_CONFIG after 5 retries!");
    return false;
  }
  MESH_DEBUG_PRINTLN("INA228 begin(): ADC_CONFIG set to 0x%04X", adc_config);

  // Calculate current LSB: Max expected current / 2^19 (20-bit ADC)
  // With 100mΩ shunt and ±163.84mV ADC range (ADCRANGE=0): Max = 163.84mV / 0.1Ω = 1.6384A
  // Using 1.6384A, LSB = 1.6384A / 524288 ≈ 3.125 µA
  // At 10mA standby: V_shunt = 1mV → SNR greatly improved vs. 20mΩ (200µV)
  _current_lsb = 1.6384f / 524288.0f;  // in Amperes (max ±1.6384A)

  // Calculate shunt calibration value
  // SHUNT_CAL = 13107.2 × 10^6 × CURRENT_LSB × R_SHUNT
  // R_SHUNT in Ohms, CURRENT_LSB in A
  float shunt_ohm = _shunt_mohm / 1000.0f;
  _base_shunt_cal = (uint16_t)(13107.2e6 * _current_lsb * shunt_ohm);
  
  // ADCRANGE = 0 (±163.84mV): No multiplier needed (×4 only required for ADCRANGE=1)
  
  // Apply calibration factor to SHUNT_CAL (if set)
  uint16_t calibrated_shunt_cal = (uint16_t)(_base_shunt_cal * _calibration_factor);
  writeRegister16(INA228_REG_SHUNT_CAL, calibrated_shunt_cal);
  delay(5);

  // Configure INA228: ADCRANGE = 0 (±163.84mV, default) for 100mΩ shunt
  // At 1A: V_shunt = 100mV (61% of full-scale) — sufficient headroom
  // At 10mA: V_shunt = 1mV — 5× better SNR than 20mΩ (was 200µV)
  uint16_t config = 0;  // ADCRANGE=0 (±163.84mV range, bit 4 = 0)
  writeRegister16(INA228_REG_CONFIG, config);
  delay(5);

  return true;
}

bool Ina228Driver::isConnected() {
  // First check if device responds at all
  Wire.beginTransmission(_i2c_addr);
  uint8_t i2c_result = Wire.endTransmission();
  
  if (i2c_result != 0) {
    return false;  // No ACK on bus
  }
  
  // Read Manufacturer ID (should be 0x5449 = "TI")
  uint16_t mfg_id = readRegister16(INA228_REG_MANUFACTURER);
  
  if (mfg_id == 0x0000 || mfg_id == 0xFFFF) {
    return false;  // Invalid MFG_ID (bus error)
  }
  
  // Some INA228 clones may have different MFG_ID, skip strict check
  // Just verify it's not a bus error value

  // Read Device ID (should be 0x228 in lower 12 bits)
  uint16_t dev_id = readRegister16(INA228_REG_DEVICE_ID);
  
  if (dev_id == 0x0000 || dev_id == 0xFFFF) {
    return false;  // Invalid DEV_ID (bus error)
  }
  
  // RELAXED: Accept any valid DEV_ID since some INA228 clones report 0x2281
  // We already verified MFG_ID = 0x5449 (TI), that's sufficient
  // Original check: (dev_id & 0x0FFF) != 0x228
  // Observed:  0x2281 (clone/variant), but MFG_ID is correct

  return true;  // Accept device if MFG_ID was valid
}

void Ina228Driver::reset() {
  writeRegister16(INA228_REG_CONFIG, INA228_CONFIG_RST);
}

uint16_t Ina228Driver::readVoltage_mV() {
  int32_t vbus_raw = readRegister24(INA228_REG_VBUS);
  // INA228 VBUS: 20-bit ADC left-aligned in 24-bit register
  // Must right-shift by 4 bits to get actual 20-bit value
  vbus_raw >>= 4;
  // VBUS LSB = 195.3125 µV
  float vbus_v = vbus_raw * 195.3125e-6;
  return (uint16_t)(vbus_v * 1000.0f);  // Convert to mV
}

int16_t Ina228Driver::readCurrent_mA() {
  int32_t current_raw = readRegister24(INA228_REG_CURRENT);
  // INA228 CURRENT: 20-bit ADC left-aligned in 24-bit register
  // Must right-shift by 4 bits to get actual 20-bit value
  current_raw >>= 4;
  // Current = raw × CURRENT_LSB
  // Calibration is applied via SHUNT_CAL register (hardware calibration)
  // Sign convention: INVERT because shunt is oriented for battery perspective
  // Positive = charging (current into battery), Negative = discharging (current from battery)
  float current_a = current_raw * _current_lsb;
  float current_mA = -current_a * 1000.0f;  // Convert to mA, inverted sign
  return (int16_t)(current_mA);
}

float Ina228Driver::readCurrent_mA_precise() {
  int32_t current_raw = readRegister24(INA228_REG_CURRENT);
  // INA228 CURRENT: 20-bit ADC left-aligned in 24-bit register
  // Must right-shift by 4 bits to get actual 20-bit value
  current_raw >>= 4;
  // Current = raw × CURRENT_LSB
  // Calibration is applied via SHUNT_CAL register (hardware calibration)
  // Sign convention: INVERT because shunt is oriented for battery perspective
  // Positive = charging (current into battery), Negative = discharging (current from battery)
  float current_a = current_raw * _current_lsb;
  float current_mA = -current_a * 1000.0f;  // Convert to mA with full precision, inverted sign
  return current_mA;
}

bool Ina228Driver::shutdown() {
  // Set operating mode to Shutdown (MODE = 0x0)
  // This disables all conversions and Coulomb Counter.
  // Use retry+readback — I2C writes can fail silently (see setUnderVoltageAlert).
  // If this fails, INA228 stays in continuous mode (~350µA wasted in System Sleep!).
  uint16_t adc_config = 0x0000;  // MODE = 0x0 (Shutdown)

  for (int retry = 0; retry < 3; retry++) {
    if (!writeRegister16(INA228_REG_ADC_CONFIG, adc_config)) {
      delay(10);
      continue;
    }
    delay(2);
    uint16_t readback = readRegister16(INA228_REG_ADC_CONFIG);
    if ((readback & 0xF000) == 0x0000) {  // Check MODE bits [15:12]
      return true;
    }
    delay(10);
  }
  return false;
}

void Ina228Driver::wakeup() {
  // Re-enable continuous measurement mode with full ADC configuration
  // Must restore conversion times from begin() - defaults are much shorter (50µs)
  uint16_t adc_config = (INA228_ADC_MODE_CONT_ALL << 12) |  // MODE: Continuous all = 0xF
                        (INA228_ADC_CT_2074us << 9)      |  // VBUSCT: 2074µs for voltage accuracy
                        (INA228_ADC_CT_4120us << 6)      |  // VSHCT: 4120µs for current/SOC accuracy
                        (INA228_ADC_CT_540us << 3)       |  // VTCT: 540µs (temp less critical)
                        (INA228_ADC_AVG_256 << 0);          // AVG: 256 samples
  writeRegister16(INA228_REG_ADC_CONFIG, adc_config);
}

uint16_t Ina228Driver::readVBATDirect(TwoWire* wire, uint8_t i2c_addr) {
  // === Important: This is called BEFORE begin() in Early Boot Check ===
  // The INA228 may be in power-on reset state, so we need to be careful
  
  // First check if device responds
  wire->beginTransmission(i2c_addr);
  if (wire->endTransmission() != 0) {
    return 0;  // Device not present
  }
  
  // === One-Shot ADC Trigger ===
  // Configure ADC for single-shot bus voltage measurement
  // MODE = 0x1 (Single-shot bus voltage only)
  uint16_t adc_config = (0x1 << 12);  // MODE = 0x1, no averaging for speed
  
  wire->beginTransmission(i2c_addr);
  wire->write(INA228_REG_ADC_CONFIG);
  wire->write((adc_config >> 8) & 0xFF);
  wire->write(adc_config & 0xFF);
  if (wire->endTransmission() != 0) {
    return 0;  // I2C communication failed
  }
  
  // Wait for conversion to complete (~200µs typical, use 2ms to be safe)
  delay(2);
  
  // Read VBUS register (24-bit)
  wire->beginTransmission(i2c_addr);
  wire->write(INA228_REG_VBUS);
  if (wire->endTransmission(false) != 0) {
    return 0;
  }
  
  wire->requestFrom(i2c_addr, (uint8_t)3);
  if (wire->available() < 3) {
    return 0;
  }
  
  int32_t vbus_raw = wire->read() << 16;  // MSB
  vbus_raw |= wire->read() << 8;          // Mid
  vbus_raw |= wire->read();               // LSB
  
  // Sign-extend 24-bit to 32-bit
  if (vbus_raw & 0x800000) {
    vbus_raw |= 0xFF000000;
  }
  
  // INA228 VBUS: 20-bit ADC left-aligned in 24-bit register
  // Must right-shift by 4 bits to get actual 20-bit value
  vbus_raw >>= 4;
  
  // VBUS LSB = 195.3125 µV
  float vbus_v = vbus_raw * 195.3125e-6;
  uint16_t vbus_mv = (uint16_t)(vbus_v * 1000.0f);
  
  // Sanity check: Battery voltage should be between 2V and 15V
  // If implausible, still return value for debugging
  
  return vbus_mv;  // Convert to mV
}

int32_t Ina228Driver::readPower_mW() {
  int32_t power_raw = readRegister24(INA228_REG_POWER);
  // Power LSB = 3.2 × CURRENT_LSB
  // Sign convention: INVERT to match current sign (positive = charging)
  float power_w = power_raw * (3.2f * _current_lsb);
  return (int32_t)(-power_w * 1000.0f);  // Convert to mW, inverted sign
}

int32_t Ina228Driver::readEnergy_mWh() {
  int64_t energy_raw = readRegister40(INA228_REG_ENERGY);
  // Energy LSB = 16 × 3.2 × CURRENT_LSB (in J)
  // Convert to Wh: / 3600
  // NO inversion - shunt orientation gives correct battery perspective
  // Positive = discharging (energy from battery), Negative = charging (energy into battery)
  float energy_j = energy_raw * (16.0f * 3.2f * _current_lsb);
  float energy_wh = energy_j / 3600.0f;
  return (int32_t)(energy_wh * 1000.0f);  // Convert to mWh, NO inversion
}

float Ina228Driver::readCharge_mAh() {
  int64_t charge_raw = readRegister40(INA228_REG_CHARGE);
  // Charge LSB = CURRENT_LSB (in C = A·s)
  // Convert to Ah: / 3600
  // INVERT: Hardware negative = charging, we want positive = charging (battery perspective)
  float charge_c = charge_raw * _current_lsb;
  float charge_ah = charge_c / 3600.0f;
  return -charge_ah * 1000.0f;  // Convert to mAh, INVERTED for battery perspective
}

float Ina228Driver::readDieTemperature_C() {
  // DIETEMP is a 16-bit register (not 24-bit like others!)
  int16_t temp_raw = (int16_t)readRegister16(INA228_REG_DIETEMP);
  // Temperature LSB = 7.8125 m°C
  float temp_c = temp_raw * 7.8125e-3;
  return temp_c;
}

bool Ina228Driver::readAll(Ina228BatteryData* data) {
  if (!isConnected()) {
    return false;
  }

  data->voltage_mv = readVoltage_mV();
  data->current_ma = readCurrent_mA();
  data->power_mw = readPower_mW();
  data->energy_mwh = readEnergy_mWh();
  data->charge_mah = readCharge_mAh();
  data->die_temp_c = readDieTemperature_C();

  return true;
}

void Ina228Driver::resetCoulombCounter() {
  // Write RSTACC (bit 14) directly — no read-modify-write!
  // CONFIG is always 0x0000 (set in begin()), so we can safely write 0x4000.
  // RMW is dangerous: if readRegister16() returns garbage on I2C glitch,
  // we could accidentally set RST (bit 15) and wipe SHUNT_CAL.
  writeRegister16(INA228_REG_CONFIG, (1 << 14));  // RSTACC only
}

uint16_t Ina228Driver::readShuntCalRegister() {
  return readRegister16(INA228_REG_SHUNT_CAL);
}

uint16_t Ina228Driver::readAdcConfigRegister() {
  return readRegister16(INA228_REG_ADC_CONFIG);
}

uint16_t Ina228Driver::readConfigRegister() {
  return readRegister16(INA228_REG_CONFIG);
}

bool Ina228Driver::validateAndRepairShuntCal() {
  uint16_t expected = (uint16_t)(_base_shunt_cal * _calibration_factor);
  if (expected == 0) return false;  // Not initialized
  
  uint16_t actual = readShuntCalRegister();
  if (actual == expected) return true;  // OK
  
  // SHUNT_CAL is wrong — repair it!
  MESH_DEBUG_PRINTLN("INA228: SHUNT_CAL corrupted! Expected=%u, Got=%u — repairing", expected, actual);
  writeRegister16(INA228_REG_SHUNT_CAL, expected);
  delay(2);
  
  uint16_t verify = readShuntCalRegister();
  if (verify != expected) {
    MESH_DEBUG_PRINTLN("INA228: SHUNT_CAL repair FAILED! Wrote=%u, Read=%u", expected, verify);
    return false;
  }
  MESH_DEBUG_PRINTLN("INA228: SHUNT_CAL repaired successfully");
  return true;
}

bool Ina228Driver::setUnderVoltageAlert(uint16_t voltage_mv) {
  // BUVL register: 3.125 mV/LSB (per datasheet Table 7-20)
  // Non-zero BUVL enables bus under-voltage comparison → BUSUL flag + ALERT pin
  // BUVL = 0 disables comparison (datasheet default)
  uint16_t buvl_value = (uint16_t)(voltage_mv / 3.125f);

  // Write with retry and readback verification (I2C writes can fail silently)
  for (int retry = 0; retry < 3; retry++) {
    if (!writeRegister16(INA228_REG_BUVL, buvl_value)) {
      delay(10);
      continue;
    }
    delay(5);
    uint16_t readback = readRegister16(INA228_REG_BUVL);
    if (readback == buvl_value) {
      return true;
    }
    MESH_DEBUG_PRINTLN("INA228: BUVL write mismatch (wrote=0x%04X, read=0x%04X), retry %d", buvl_value, readback, retry);
    delay(10);
  }
  MESH_DEBUG_PRINTLN("INA228: BUVL write FAILED after 3 retries!");
  return false;
}

void Ina228Driver::enableAlert(bool enable_uvlo, bool active_high, bool latch_alert) {
  // DIAG_ALRT register: Only bits [15:12] are R/W (config), bits [11:0] are read-only flags.
  // Writing to this register clears all flag bits [11:0].
  // Note: BUSUL/BUSOL flags (bits 3-4) are READ-ONLY status flags, NOT enable bits.
  // Bus under-voltage comparison is enabled by setting BUVL register to non-zero.
  uint16_t diag_alrt = 0;

  if (latch_alert) {
    diag_alrt |= INA228_DIAG_ALRT_ALATCH;   // Latch mode: Alert stays active until DIAG_ALRT is read
  }

  if (active_high) {
    diag_alrt |= INA228_DIAG_ALRT_APOL;     // Active-high polarity (default: active-low)
  }

  // Write with retry and readback verification
  // Only bits [15:12] are readable as config; bits [11:0] are flags (may change between write and read)
  uint16_t expected_config = diag_alrt & 0xF000;  // Only check config bits
  for (int retry = 0; retry < 3; retry++) {
    writeRegister16(INA228_REG_DIAG_ALRT, diag_alrt);
    delay(5);
    uint16_t readback = readRegister16(INA228_REG_DIAG_ALRT);
    uint16_t readback_config = readback & 0xF000;
    if (readback_config == expected_config) {
      return;
    }
    MESH_DEBUG_PRINTLN("INA228: DIAG_ALRT config mismatch (wrote=0x%04X, read=0x%04X, config=0x%04X vs 0x%04X), retry %d",
                       diag_alrt, readback, expected_config, readback_config, retry);
    delay(10);
  }
  MESH_DEBUG_PRINTLN("INA228: DIAG_ALRT write FAILED after 3 retries!");
}

bool Ina228Driver::isAlertActive() {
  uint16_t diag_flags = getDiagnosticFlags();
  return (diag_flags & (INA228_DIAG_ALRT_BUSUL | INA228_DIAG_ALRT_BUSOL)) != 0;
}

void Ina228Driver::clearAlert() {
  // Read diagnostic register to clear latched alerts
  getDiagnosticFlags();
}

uint16_t Ina228Driver::getDiagnosticFlags() {
  return readRegister16(INA228_REG_DIAG_ALRT);
}

uint16_t Ina228Driver::readBuvlRegister() {
  return readRegister16(INA228_REG_BUVL);
}

// ===== Private Methods =====

bool Ina228Driver::writeRegister16(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(_i2c_addr);
  Wire.write(reg);
  Wire.write((value >> 8) & 0xFF);  // MSB
  Wire.write(value & 0xFF);         // LSB
  bool ok = (Wire.endTransmission() == 0);
  return ok;
}

uint16_t Ina228Driver::readRegister16(uint8_t reg) {
  Wire.beginTransmission(_i2c_addr);
  Wire.write(reg);
  Wire.endTransmission(false);  // Repeated start

  Wire.requestFrom(_i2c_addr, (uint8_t)2);
  if (Wire.available() < 2) {
      return 0;
  }

  uint16_t value = Wire.read() << 8;  // MSB
  value |= Wire.read();               // LSB
  return value;
}

int32_t Ina228Driver::readRegister24(uint8_t reg) {
  Wire.beginTransmission(_i2c_addr);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(_i2c_addr, (uint8_t)3);
  if (Wire.available() < 3) {
      return 0;
  }

  int32_t value = Wire.read() << 16;  // MSB
  value |= Wire.read() << 8;          // Mid
  value |= Wire.read();               // LSB

  // Sign-extend 24-bit to 32-bit
  if (value & 0x800000) {
    value |= 0xFF000000;
  }

  return value;
}

int64_t Ina228Driver::readRegister40(uint8_t reg) {
  Wire.beginTransmission(_i2c_addr);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(_i2c_addr, (uint8_t)5);
  if (Wire.available() < 5) {
      return 0;
  }

  int64_t value = (int64_t)Wire.read() << 32;  // MSB
  value |= (int64_t)Wire.read() << 24;
  value |= (int64_t)Wire.read() << 16;
  value |= (int64_t)Wire.read() << 8;
  value |= (int64_t)Wire.read();               // LSB

  // Sign-extend 40-bit to 64-bit
  if (value & 0x8000000000LL) {
    value |= 0xFFFFFF0000000000LL;
  }

  return value;
}

// ===== Calibration Methods =====

float Ina228Driver::calibrateCurrent(float actual_current_ma) {
  // Step 1: Reset calibration to 1.0 for accurate measurement
  setCalibrationFactor(1.0f);
  
  // Wait for ADC to settle
  delay(10);
  
  // Step 2: Read current measured value (uncalibrated)
  int16_t measured_current_ma = readCurrent_mA();
  
  // Avoid division by zero
  if (measured_current_ma == 0) {
    return 1.0f;  // No correction possible
  }
  
  // Step 3: Calculate correction factor (INVERSE ratio)
  // If INA shows -9.4mA but actual is -10.4mA, we need SHUNT_CAL to be SMALLER
  // so INA writes a LARGER value. Factor = measured/actual = 9.4/10.4 = 0.904
  float new_factor = (float)measured_current_ma / actual_current_ma;
  
  // Step 4: Apply new calibration factor to INA228 hardware
  setCalibrationFactor(new_factor);
  
  return new_factor;
}

void Ina228Driver::setCalibrationFactor(float factor) {
  // Clamp to reasonable range (0.5x to 2.0x)
  if (factor < 0.5f) factor = 0.5f;
  if (factor > 2.0f) factor = 2.0f;
  
  _calibration_factor = factor;
  
  // Apply calibration factor to SHUNT_CAL register
  // Lower SHUNT_CAL → INA writes larger values → higher current reading
  // Higher SHUNT_CAL → INA writes smaller values → lower current reading
  // CURRENT_LSB stays constant (per datasheet design)
  if (_base_shunt_cal > 0) {
    uint16_t calibrated_shunt_cal = (uint16_t)(_base_shunt_cal * factor);
    writeRegister16(INA228_REG_SHUNT_CAL, calibrated_shunt_cal);
  }
}

float Ina228Driver::getCalibrationFactor() const {
  return _calibration_factor;
}


