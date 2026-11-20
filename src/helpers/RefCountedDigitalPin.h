#pragma once

#include <Arduino.h>

class RefCountedDigitalPin {
  uint8_t _pin;
  int8_t _claims = 0;
  uint8_t _active = 0;
public:
  RefCountedDigitalPin(uint8_t pin,uint8_t active=HIGH): _pin(pin), _active(active) { }

  void begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, !_active);  // initial state
  }

  // Both set the level, then make the pin an output again, since other code may have left it
  // an input. On a RAK19007 with a RAK12501, WB_IO2 left as an input keeps 3V3_S in its last state.
  void claim() {
    _claims++;
    if (_claims > 0) {
      digitalWrite(_pin, _active);
      pinMode(_pin, OUTPUT);
    }
  }

  void release() {
    if (_claims == 0) return; // avoid negative _claims

    _claims--;
    if (_claims == 0) {
      digitalWrite(_pin, !_active);
      pinMode(_pin, OUTPUT);
    }
  }
};
