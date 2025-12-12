#pragma once
// Based off here https://github.com/espressif/arduino-esp32/blob/d1eb62d7c6dda16c254c374504aa93188d7c386b/variants/arduino_nesso_n1/expander.cpp
// Should be OK based on this? https://opensource.stackexchange.com/a/6406

#include "pins_arduino.h"
#include <helpers/ESP32Board.h>
#include <Wire.h>

static bool wireInitialized = true; // initialised in ESP32Board.begin() ; ToDo: Remove all these conditions in future
static bool expanderInitialized = false;

// From https://www.diodes.com/datasheet/download/PI4IOE5V6408.pdf
static void writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static uint8_t readRegister(uint8_t address, uint8_t reg) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(address, 1);
  return Wire.read();
}

static void writeBitRegister(uint8_t address, uint8_t reg, uint8_t bit, uint8_t value) {
  MESH_DEBUG_PRINTLN("ExpanderPin writeBitRegister(address=%u, reg=%u, bit=%u, value=%u)", address, reg, bit, value);
  uint8_t val = readRegister(address, reg);
  if (value) {
    writeRegister(address, reg, val | (1 << bit));
  } else {
    writeRegister(address, reg, val & ~(1 << bit));
  }
}

static bool readBitRegister(uint8_t address, uint8_t reg, uint8_t bit) {
  MESH_DEBUG_PRINTLN("ExpanderPin readBitRegister(address=%u, reg=%u, bit=%u)", address, reg, bit);
  uint8_t val = readRegister(address, reg);
  return ((val & (1 << bit)) > 0);
}

void pinMode(ExpanderPin pin, uint8_t mode) {
  if (!wireInitialized) {
    Wire.begin(SDA, SCL);
    wireInitialized = true;
    // reset all registers to default state
  }
  if (!expanderInitialized) {
    writeRegister(pin.address, 0x1, 0x1);
    // set all pins as high as default state
    writeRegister(pin.address, 0x9, 0xFF);
    // interrupt mask to all pins
    writeRegister(pin.address, 0x11, 0xFF);
    // all input
    writeRegister(pin.address, 0x3, 0);
    expanderInitialized = true;
  }
  MESH_DEBUG_PRINTLN("ExpanderPin pinMode(pin=%u, mode=%u)", pin.pin, mode);
  writeBitRegister(pin.address, 0x3, pin.pin, mode == OUTPUT);
  if (mode == OUTPUT) {
    // remove high impedance
    writeBitRegister(pin.address, 0x7, pin.pin, false);
  } else if (mode == INPUT_PULLUP) {
    // set pull-up resistor
    writeBitRegister(pin.address, 0xB, pin.pin, true);
    writeBitRegister(pin.address, 0xD, pin.pin, true);
  } else if (mode == INPUT_PULLDOWN) {
    // disable pull-up resistor
    writeBitRegister(pin.address, 0xB, pin.pin, true);
    writeBitRegister(pin.address, 0xD, pin.pin, false);
  } else if (mode == INPUT) {
    // disable pull selector resistor
    writeBitRegister(pin.address, 0xB, pin.pin, false);
  }
}

void digitalWrite(ExpanderPin pin, uint8_t val) {
  if (!wireInitialized) {
    Wire.begin(SDA, SCL);
    wireInitialized = true;
  }
  MESH_DEBUG_PRINTLN("ExpanderPin digitalWrite(%u)", pin.pin);
  writeBitRegister(pin.address, 0x5, pin.pin, val == HIGH);
}

int digitalRead(ExpanderPin pin) {
  if (!wireInitialized) {
    Wire.begin(SDA, SCL);
    wireInitialized = true;
  }
  MESH_DEBUG_PRINTLN("ExpanderPin digitalRead(%u)", pin.pin);
  return readBitRegister(pin.address, 0xF, pin.pin);
}

void NessoBattery::begin() {
  // AW32001E - address 0x49
  // Spec: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/core/LLM630%20Computer%20Kit/AW32001E.pdf
  if (!wireInitialized) {
    Wire.begin(SDA, SCL);
    wireInitialized = true;
  }

  uint8_t val = 0;
  val = readRegister(AW32001_I2C_CHIP_ADDR, AW32001_REG_CHIP_ID);
  // coarsely check if chip is actually the right chip
  auto res = (val == AW32001_I2C_CHIP_ADDR);
  if (res) {
    val = readRegister(AW32001_I2C_CHIP_ADDR, AW32001_REG_CHR_TMR);
#ifdef MESH_DEBUG
    // Debug output the WatchDog Timer (wdt) state
    MESH_DEBUG_PRINTLN("NessoBattery.begin(): CHR_TMR full register; bits 5,6 are for WDT = %#02x", val);
#endif
    // disable WatchDog Timer (wdt)
    // take existing register value AND with 00011111
    val = val & 0x1f;
    writeRegister(AW32001_I2C_CHIP_ADDR, AW32001_REG_CHR_TMR, val);
  }
#ifdef MESH_DEBUG
  else {
    MESH_DEBUG_PRINTLN("NessoBattery.begin(): Register of chip ADDR = %u != I2C of chip %u", AW32001_REG_CHIP_ID, AW32001_I2C_CHIP_ADDR);
  }
#endif

  // store if chip is initiated by whether it passed above checks and had watchdog disabled
  _power_mgmt_init = res;
}

void NessoBattery::enableCharge() {
  // AW32001E - address 0x49
  // set CEB (charge enable) bit (3) low (0) in AW32001_REG_PWR_CFG (0x01)
  MESH_DEBUG_PRINTLN("NessoBattery::enableCharge()");

  if (_power_mgmt_init) {
    MESH_DEBUG_PRINTLN("NessoBattery::enableCharge(): _power_mgmt_init = true");
    if (!wireInitialized) {
      Wire.begin(SDA, SCL);
      wireInitialized = true;
    }

    bool charge_enable_bit = readBitRegister(AW32001_I2C_CHIP_ADDR, AW32001_REG_PWR_CFG, 3);
    MESH_DEBUG_PRINTLN("NessoBattery::enableCharge(): Current charge setting (low is on): %u", charge_enable_bit);
    MESH_DEBUG_PRINTLN("NessoBattery::enableCharge(): isCharging(): %u", NessoBattery::isCharging());
    MESH_DEBUG_PRINTLN("NessoBattery::enableCharge(): Current charge level %u %%", NessoBattery::getChargeLevel());
    MESH_DEBUG_PRINTLN("NessoBattery::enableCharge(): Current voltage %f V", NessoBattery::getVoltage());
    MESH_DEBUG_PRINTLN("NessoBattery::enableCharge(): Current voltage %u mV", NessoBattery::getMilliVoltage());

    writeBitRegister(AW32001_I2C_CHIP_ADDR, AW32001_REG_PWR_CFG, 3, false);
  }
#ifdef MESH_DEBUG
  else {
    MESH_DEBUG_PRINTLN("NessoBattery::enableCharge(): _power_mgmt_init is false, won't enable charge");
  }
#endif
}

NessoBattery::ChargeStatus NessoBattery::getChargeStatus(void) {
  uint8_t reg_value = 0;
  if (_power_mgmt_init)
  {
    reg_value = readRegister(AW32001_I2C_CHIP_ADDR, AW32001_REG_SYS_STA);
    // Extract bits 4 and 3 for charge status
    reg_value = (reg_value >> 3) & 0b00000011; // Get bits 4 and 3
    MESH_DEBUG_PRINTLN("NessoBattery::getChargeStatus(): bits 4 and 3 from register %#02x = %u", AW32001_REG_SYS_STA, reg_value);
    switch (reg_value)
    {
      case 0b00: return CS_NOT_CHARGING; // Not charging
      case 0b01: return CS_PRE_CHARGE;   // Pre-charge
      case 0b10: return CS_CHARGE;       // Charging
      case 0b11: return CS_CHARGE_DONE;  // Charge done
      default: return CS_UNKNOWN;        // Unknown state
    }
  }
  MESH_DEBUG_PRINTLN("NessoBattery::getChargeStatus(): failed, probably chip wasn't init");
  return CS_UNKNOWN; // Return unknown if read failed
}

bool NessoBattery::isCharging(void)
{
  ChargeStatus status = getChargeStatus();
  MESH_DEBUG_PRINTLN("NessoBattery::isCharging(): ChargeStatus = %u; is? false0/true1 = %u", status, (status == CS_PRE_CHARGE || status == CS_CHARGE));
  return (status == CS_PRE_CHARGE || status == CS_CHARGE);
}

float NessoBattery::getVoltage() {
  // BQ27220 - address 0x55
  if (!wireInitialized) {
    Wire.begin(SDA, SCL);
    wireInitialized = true;
  }
  MESH_DEBUG_PRINTLN("NessoBattery::getVoltage()");
  uint16_t voltage = (readRegister(0x55, 0x9) << 8) | readRegister(0x55, 0x8);
  MESH_DEBUG_PRINTLN("NessoBattery::getVoltage(): %f", voltage / 1000.0f);
  return (float)voltage / 1000.0f;
}

uint16_t NessoBattery::getMilliVoltage() {
  // BQ27220 - address 0x55
  if (!wireInitialized) {
    Wire.begin(SDA, SCL);
    wireInitialized = true;
  }
  MESH_DEBUG_PRINTLN("NessoBattery::getMilliVoltage()");
  uint16_t voltage = (readRegister(0x55, 0x9) << 8) | readRegister(0x55, 0x8);
  MESH_DEBUG_PRINTLN("NessoBattery::getMilliVoltage(): %u", voltage);
  return voltage;
}

uint16_t NessoBattery::getChargeLevel() {
  // BQ27220 - address 0x55
  if (!wireInitialized) {
    Wire.begin(SDA, SCL);
    wireInitialized = true;
  }
  MESH_DEBUG_PRINTLN("NessoBattery::getChargeLevel()");
  uint16_t current_capacity = readRegister(0x55, 0x11) << 8 | readRegister(0x55, 0x10);
  uint16_t total_capacity = readRegister(0x55, 0x13) << 8 | readRegister(0x55, 0x12);
  MESH_DEBUG_PRINTLN("NessoBattery::getChargeLevel(): curr = %u / total = %u; pct = %u %%", current_capacity, total_capacity, ((current_capacity * 100) / total_capacity));
  return (current_capacity * 100) / total_capacity;
}

ExpanderPin LORA_LNA_ENABLE(5);
ExpanderPin LORA_ANTENNA_SWITCH(6);
ExpanderPin LORA_ENABLE(7);
ExpanderPin KEY1(0);
ExpanderPin KEY2(1);
ExpanderPin POWEROFF((1 << 8) | 0);
ExpanderPin LCD_RESET((1 << 8) | 1);
ExpanderPin GROVE_POWER_EN((1 << 8) | 2);
ExpanderPin VIN_DETECT((1 << 8) | 5);
ExpanderPin LCD_BACKLIGHT((1 << 8) | 6);
ExpanderPin LED_BUILTIN((1 << 8) | 7);