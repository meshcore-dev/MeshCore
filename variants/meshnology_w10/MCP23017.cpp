#include "MCP23017.h"
#include <MeshCore.h>

namespace {
constexpr uint8_t MCP23017_IODIRA_REG = 0x00;
constexpr uint8_t MCP23017_GPINTENA_REG = 0x04;
constexpr uint8_t MCP23017_GPINTENB_REG = 0x05;
constexpr uint8_t MCP23017_IOCON_REG = 0x0A;
constexpr uint8_t MCP23017_GPPUA_REG = 0x0C;
constexpr uint8_t MCP23017_GPIOA_REG = 0x12;
constexpr uint8_t MCP23017_OLATA_REG = 0x14;
constexpr uint8_t MCP23017_IOCON_BANK1_REG = 0x05;
}

bool MCP23017::readRegister(uint8_t reg, uint8_t& value) {
  _wire.beginTransmission(_address);
  _wire.write(reg);
  if (_wire.endTransmission() == 0 && _wire.requestFrom(_address, (uint8_t)1) == 1) {
    int data = _wire.read();
    if (data >= 0) {
      value = data;
      return true;
    }
  }
  _errors++;
  MESH_DEBUG_PRINTLN("W10: MCP23017 read failed at register 0x%02x", reg);
  return false;
}

bool MCP23017::writeRegister(uint8_t reg, uint8_t value) {
  _wire.beginTransmission(_address);
  _wire.write(reg);
  _wire.write(value);
  if (_wire.endTransmission() == 0) return true;
  _errors++;
  MESH_DEBUG_PRINTLN("W10: MCP23017 write failed at register 0x%02x", reg);
  return false;
}

bool MCP23017::updateRegister(uint8_t reg, uint8_t mask, bool set) {
  uint8_t value;
  if (!readRegister(reg, value)) return false;
  return writeRegister(reg, set ? value | mask : value & ~mask);
}

bool MCP23017::begin() {
  // BANK=1 IOCON aliases BANK=0 GPINTENB; safe to clear with /INT unconnected
  return writeRegister(MCP23017_IOCON_BANK1_REG, 0) && writeRegister(MCP23017_IOCON_REG, 0)
      && writeRegister(MCP23017_GPINTENA_REG, 0) && writeRegister(MCP23017_GPINTENB_REG, 0);
}

bool MCP23017::pinMode(uint8_t pin, uint8_t mode) {
  if (pin >= PIN_COUNT || (mode != INPUT && mode != INPUT_PULLUP && mode != OUTPUT)) return false;
  uint8_t bank = pin / 8;
  uint8_t mask = 1U << (pin % 8);
  return updateRegister(MCP23017_GPPUA_REG + bank, mask, mode == INPUT_PULLUP)
      && updateRegister(MCP23017_IODIRA_REG + bank, mask, mode != OUTPUT);
}

bool MCP23017::digitalWrite(uint8_t pin, uint8_t value) {
  if (pin >= PIN_COUNT) return false;
  return updateRegister(MCP23017_OLATA_REG + pin / 8, 1U << (pin % 8), value != LOW);
}

bool MCP23017::digitalRead(uint8_t pin, uint8_t& value) {
  if (pin >= PIN_COUNT) return false;
  uint8_t data;
  if (!readRegister(MCP23017_GPIOA_REG + pin / 8, data)) return false;
  value = (data & (1U << (pin % 8))) ? HIGH : LOW;
  return true;
}
