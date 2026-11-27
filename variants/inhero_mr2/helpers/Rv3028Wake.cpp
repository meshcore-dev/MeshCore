/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "Rv3028Wake.h"

#include "../InheroMr2Board.h"  // RTC_I2C_ADDR + RV3028_REG_*

#include <MeshCore.h>
#include <Wire.h>

namespace inhero {
namespace {

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(RTC_I2C_ADDR);
  bool queued = Wire.write(reg) == 1;
  queued = (Wire.write(value) == 1) && queued;
  return Wire.endTransmission() == 0 && queued;
}

bool readRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(RTC_I2C_ADDR);
  bool queued = Wire.write(reg) == 1;
  // STOP also terminates the transfer if queuing the register address failed.
  if (Wire.endTransmission() != 0 || !queued) return false;
  if (Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)1) != 1 || Wire.available() != 1) {
    while (Wire.available()) Wire.read();
    return false;
  }
  value = Wire.read();
  return true;
}

bool verifyRegister(uint8_t reg, uint8_t expected, uint8_t mask = 0xFF) {
  uint8_t value;
  return readRegister(reg, value) && (value & mask) == (expected & mask);
}

bool writeVerified(uint8_t reg, uint8_t value, uint8_t mask = 0xFF) {
  return writeRegister(reg, value) && verifyRegister(reg, value, mask);
}

} // namespace

bool configurePeriodicWake(uint16_t minutes) {
  uint16_t ticks = (minutes == 0) ? 1 : minutes;
  if (ticks > 4095) ticks = 4095;  // 12-bit register

  MESH_DEBUG_PRINTLN("PWRMGT: Configuring RTC wake in %u minutes",
                     static_cast<unsigned>(ticks));

  // Per RV-3028 manual section 4.8.2:
  // Step 1: Stop Timer and clear flags. Verify the stopped state: if TE never
  // goes LOW, writing TE=1 later would not reliably start a fresh countdown.
  // Control 1 bit 6 is reserved; only TF is relevant in the status register.
  if (!writeVerified(RV3028_REG_CTRL1, 0x00, 0xBF) ||
      !writeVerified(RV3028_REG_CTRL2, 0x00) ||
      !writeVerified(RV3028_REG_STATUS, 0x00, 0x08)) return false;

  // Step 2: Set Timer Value (ticks at 1/60 Hz)
  Wire.beginTransmission(RTC_I2C_ADDR);
  bool queued = Wire.write(RV3028_REG_TIMER_VALUE_0) == 1;
  queued = (Wire.write(ticks & 0xFF) == 1) && queued;
  queued = (Wire.write((ticks >> 8) & 0x0F) == 1) && queued;
  if (Wire.endTransmission() != 0 || !queued ||
      !verifyRegister(RV3028_REG_TIMER_VALUE_0, ticks & 0xFF) ||
      !verifyRegister(RV3028_REG_TIMER_VALUE_1, (ticks >> 8) & 0x0F, 0x0F)) return false;

  // Step 3: Enable timer (1/60 Hz, single shot)
  if (!writeRegister(RV3028_REG_CTRL1, 0x07)) return false;

  // Step 4: Enable timer interrupt
  if (!writeRegister(RV3028_REG_CTRL2, 0x10)) return false;

  // Verify the final state before allowing System OFF. Read the preset (0A/0B),
  // not the live countdown (0C/0D), and reject a timer that already expired.
  if (!verifyRegister(RV3028_REG_TIMER_VALUE_0, ticks & 0xFF) ||
      !verifyRegister(RV3028_REG_TIMER_VALUE_1, (ticks >> 8) & 0x0F, 0x0F) ||
      !verifyRegister(RV3028_REG_CTRL1, 0x07, 0xBF) ||
      !verifyRegister(RV3028_REG_CTRL2, 0x10) ||
      !verifyRegister(RV3028_REG_STATUS, 0x00, 0x08)) return false;

  MESH_DEBUG_PRINTLN("PWRMGT: RTC countdown configured (%u ticks at 1/60 Hz)", ticks);
  return true;
}

void clearTimerFlag() {
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_STATUS);
  if (Wire.endTransmission(false) != 0) return;

  Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)1);
  if (!Wire.available()) return;

  uint8_t status = Wire.read();
  if ((status & (1 << 3)) == 0) return;  // TF already clear

  status &= ~(1 << 3);
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_STATUS);
  Wire.write(status);
  Wire.endTransmission();
}

} // namespace inhero
