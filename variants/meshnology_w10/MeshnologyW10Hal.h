#pragma once

#include "MCP23017.h"
#include <RadioLib.h>

class MeshnologyW10Hal : public ArduinoHal {
  MCP23017& _io;

public:
  MeshnologyW10Hal(SPIClass& spi, MCP23017& io)
    : ArduinoHal(spi, SPISettings(8000000, MSBFIRST, SPI_MODE0)), _io(io) { }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (isExpanderVirtualPin(pin)) {
      _io.pinMode(expanderPin(pin), mode);
    } else {
      ArduinoHal::pinMode(pin, mode);
    }
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (isExpanderVirtualPin(pin)) {
      _io.digitalWrite(expanderPin(pin), value);
    } else {
      ArduinoHal::digitalWrite(pin, value);
    }
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (!isExpanderVirtualPin(pin)) return ArduinoHal::digitalRead(pin);
    uint8_t value;
    if (_io.digitalRead(expanderPin(pin), value)) return value;
    // Failed reads cannot release BUSY or fabricate a DIO1 completion, including blocking CAD
    return pin == P_LORA_BUSY ? HIGH : LOW;
  }

  uint32_t pinToInterrupt(uint32_t pin) override {
    return isExpanderVirtualPin(pin) ? RADIOLIB_NC : ArduinoHal::pinToInterrupt(pin);
  }

  void attachInterrupt(uint32_t pin, void (*callback)(), uint32_t mode) override {
    if (pin != RADIOLIB_NC && !isExpanderVirtualPin(pin)) ArduinoHal::attachInterrupt(pin, callback, mode);
  }

  void detachInterrupt(uint32_t pin) override {
    if (pin != RADIOLIB_NC && !isExpanderVirtualPin(pin)) ArduinoHal::detachInterrupt(pin);
  }
};
