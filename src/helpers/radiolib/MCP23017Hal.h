#pragma once

#include <helpers/MCP23017.h>
#include <RadioLib.h>

class MCP23017Hal : public ArduinoHal {
  MCP23017& _io;
  uint32_t _busy_pin;
  uint32_t _irq_pin = RADIOLIB_NC;
  uint32_t _irq_mode = RISING;
  void (*_callback)() = nullptr;

public:
  // RadioLib and board code address expander lines as virtual pins PIN_BASE + n,
  // n in [0, MCP23017::PIN_COUNT); this HAL routes those to the driver.
  static constexpr uint32_t PIN_BASE = 100;
  static constexpr bool isVirtualPin(uint32_t pin) {
    return pin >= PIN_BASE && pin < PIN_BASE + MCP23017::PIN_COUNT;
  }
  static constexpr uint8_t expanderPin(uint32_t pin) { return static_cast<uint8_t>(pin - PIN_BASE); }

  MCP23017Hal(SPIClass& spi, MCP23017& io, uint32_t busy_pin, SPISettings settings = RADIOLIB_DEFAULT_SPI_SETTINGS)
    : ArduinoHal(spi, settings), _io(io), _busy_pin(busy_pin) { }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (isVirtualPin(pin)) {
      _io.pinMode(expanderPin(pin), mode);
    } else {
      ArduinoHal::pinMode(pin, mode);
    }
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (isVirtualPin(pin)) {
      _io.digitalWrite(expanderPin(pin), value);
    } else {
      ArduinoHal::digitalWrite(pin, value);
    }
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (!isVirtualPin(pin)) return ArduinoHal::digitalRead(pin);
    uint8_t value;
    if (_io.digitalRead(expanderPin(pin), value)) return value;
    // Failed reads cannot release BUSY or fabricate a DIO1 completion, including blocking CAD
    return pin == _busy_pin ? HIGH : LOW;
  }

  uint32_t pinToInterrupt(uint32_t pin) override {
    return isVirtualPin(pin) ? pin : ArduinoHal::pinToInterrupt(pin);
  }

  void attachInterrupt(uint32_t pin, void (*callback)(), uint32_t mode) override {
    if (isVirtualPin(pin)) {
      _irq_pin = pin;
      _irq_mode = mode;
      _callback = callback;
    } else if (pin != RADIOLIB_NC) {
      ArduinoHal::attachInterrupt(pin, callback, mode);
    }
  }

  void detachInterrupt(uint32_t pin) override {
    if (isVirtualPin(pin)) {
      if (pin == _irq_pin) {
        _callback = nullptr;
        _irq_pin = RADIOLIB_NC;
      }
    } else if (pin != RADIOLIB_NC) {
      ArduinoHal::detachInterrupt(pin);
    }
  }

  // Poll one radio IRQ held active until RadioLib clears it. Sampling edges
  // would miss a new IRQ if the line cleared and reasserted between polls.
  void poll() {
    if (!_callback) return;
    uint8_t value;
    if (!_io.digitalRead(expanderPin(_irq_pin), value)) return;
    if ((_irq_mode == RISING && value == HIGH) || (_irq_mode == FALLING && value == LOW)) {
      _callback();
    }
  }
};
