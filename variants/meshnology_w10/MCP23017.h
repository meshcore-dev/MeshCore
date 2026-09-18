#pragma once

#include <Arduino.h>
#include <Wire.h>

class MCP23017 {
  TwoWire& _wire;
  uint8_t _address;
  uint32_t _errors = 0;

  bool readRegister(uint8_t reg, uint8_t& value);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool updateRegister(uint8_t reg, uint8_t mask, bool set);

public:
  static constexpr uint8_t PIN_COUNT = 16;   // GPA0..GPA7, GPB0..GPB7

  MCP23017(TwoWire& wire, uint8_t address = 0x20) : _wire(wire), _address(address) { }

  bool begin();
  bool pinMode(uint8_t pin, uint8_t mode);
  bool digitalWrite(uint8_t pin, uint8_t value);
  bool digitalRead(uint8_t pin, uint8_t& value);
  uint32_t getErrorCount() const { return _errors; }
};

// Board and RadioLib code address expander lines as virtual pins EXPANDER_PIN_BASE + n,
// n in [0, MCP23017::PIN_COUNT); MeshnologyW10Hal routes those to the driver.
constexpr uint8_t EXPANDER_PIN_BASE = 100;
constexpr bool isExpanderVirtualPin(uint32_t pin) {
  return pin >= EXPANDER_PIN_BASE && pin < static_cast<uint32_t>(EXPANDER_PIN_BASE + MCP23017::PIN_COUNT);
}
constexpr uint8_t expanderPin(uint32_t pin) { return static_cast<uint8_t>(pin - EXPANDER_PIN_BASE); }
