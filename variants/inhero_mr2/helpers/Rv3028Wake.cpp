/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "Rv3028Wake.h"

#include "../InheroMr2Board.h"  // RTC_I2C_ADDR + RV3028_REG_*

#include <MeshCore.h>
#include <Wire.h>

namespace inhero {

void configurePeriodicWake(uint16_t minutes) {
  uint16_t ticks = (minutes == 0) ? 1 : minutes;
  if (ticks > 4095) ticks = 4095;  // 12-bit register

  MESH_DEBUG_PRINTLN("PWRMGT: Configuring RTC wake in %u minutes",
                     static_cast<unsigned>(ticks));

  // Per RV-3028 manual section 4.8.2:
  // Step 1: Stop Timer and clear flags
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL1);
  Wire.write(0x00); // TE=0, TD=00 (stop timer)
  Wire.endTransmission();

  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL2);
  Wire.write(0x00); // TIE=0
  Wire.endTransmission();

  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_STATUS);
  Wire.write(0x00); // Clear TF
  Wire.endTransmission();

  // Step 2: Set Timer Value (ticks at 1/60 Hz)
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_TIMER_VALUE_0);
  Wire.write(ticks & 0xFF);
  Wire.write((ticks >> 8) & 0x0F);
  Wire.endTransmission();

  // Step 3: Enable timer (1/60 Hz, single shot)
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL1);
  Wire.write(0x07); // TE=1, TD=11 (1/60 Hz), TRPT=0 (single shot)
  Wire.endTransmission();

  // Step 4: Enable timer interrupt
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL2);
  Wire.write(0x10); // TIE=1
  Wire.endTransmission();

  MESH_DEBUG_PRINTLN("PWRMGT: RTC countdown configured (%u ticks at 1/60 Hz)", ticks);
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
