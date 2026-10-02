/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "BqLowPowerSetup.h"

#include <Arduino.h>
#include <Wire.h>
#include <MeshCore.h>

#include "../InheroMr2Board.h"
#include "../lib/BqDriver.h"

namespace inhero {

static constexpr uint8_t INA228_ADDR = 0x40;

namespace {
bool readBq(uint8_t reg, uint8_t* data, uint8_t count = 1) {
  Wire.beginTransmission(BQ25798_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)BQ25798_I2C_ADDR, count) != count) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}

bool writeBq(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BQ25798_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
}

void maintainSolarDuringLowVoltageWake(bool mpptEnabled) {
  // The caller already checked charge_enable and restored the CE output.
  if (!mpptEnabled) return;

  // At deep discharge the charger may be unpowered. Never use failed reads
  // as status or ADC data, and never initialise/reset its retained settings.
  Wire.beginTransmission(BQ25798_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    MESH_DEBUG_PRINTLN("LV-Wake: BQ unavailable, skipping solar maintenance");
    return;
  }

  // Do not require an ADC conversion: low VBAT + HIZ can prevent it from
  // starting. Source qualification itself rejects missing/weak input power.
  uint8_t status;
  if (!readBq(0x1B, &status)) return;
  if (!(status & 0x08)) {
    uint8_t control;
    if (!readBq(BQ25798_REG_CHARGER_CONTROL_0, &control)) return;
    if (!writeBq(BQ25798_REG_CHARGER_CONTROL_0, control | 0x04)) return;
    delay(50);
    // Retry clearing HIZ on transient bus errors so charging can resume.
    bool cleared = false;
    for (int retry = 0; retry < 3; ++retry) {
      if (readBq(BQ25798_REG_CHARGER_CONTROL_0, &control) &&
          writeBq(BQ25798_REG_CHARGER_CONTROL_0, control & ~0x04)) {
        cleared = true;
        break;
      }
      delay(10);
    }
    if (!cleared) return;
    MESH_DEBUG_PRINTLN("LV-Wake: PG=0, toggled HIZ");
    uint32_t start = millis();
    do {
      delay(20);
      if (!readBq(0x1B, &status)) return;
      if (status & 0x08) break;
    } while (millis() - start < 1000);
  }
  if (!(status & 0x08)) return;
  uint8_t mppt;
  if (readBq(0x15, &mppt) && !(mppt & 0x01)) {
    // Below VSYSMIN the BQ may immediately reset EN_MPPT. Verify before logging.
    if (writeBq(0x15, mppt | 0x01) && readBq(0x15, &mppt) && (mppt & 0x01))
      MESH_DEBUG_PRINTLN("LV-Wake: MPPT re-enabled");
  }
}

void prepareIcsForSystemOff() {
  // INA228 -> shutdown mode (~3.5uA vs ~350uA continuous).
  // I2C writes can fail silently -> retry with readback verification.
  for (int retry = 0; retry < 3; retry++) {
    Wire.beginTransmission(INA228_ADDR);
    Wire.write(0x01);  // ADC_CONFIG
    Wire.write(0x00);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
      delay(10);
      continue;
    }
    delay(2);
    Wire.beginTransmission(INA228_ADDR);
    Wire.write(0x01);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)INA228_ADDR, (uint8_t)2);
    uint16_t rb = 0;
    if (Wire.available() >= 2) {
      rb = (Wire.read() << 8) | Wire.read();
    }
    if ((rb & 0xF000) == 0x0000) break;
    delay(10);
  }

  // INA228 -> release latched ALERT (under-voltage alert is ALATCH=1 -> ALERT stays LOW
  // -> RAK4630 internal pull-up wastes ~330uA). Switch to transparent mode and
  // zero the threshold so no condition can re-assert.
  Wire.beginTransmission(INA228_ADDR);
  Wire.write(0x0B);  // DIAG_ALRT
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.beginTransmission(INA228_ADDR);
  Wire.write(0x08);  // BUVL
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.endTransmission();

  // BQ25798 -> low-power housekeeping. These static helpers use raw Wire and
  // work even when the driver instance has not been constructed yet (LV-Wake).
  BqDriver::disableAdc();           // ~500uA saving
  BqDriver::maskAllInterrupts();    // prevent INT holding LOW
  BqDriver::clearInterruptFlags();  // de-assert latched INT
}

} // namespace inhero
