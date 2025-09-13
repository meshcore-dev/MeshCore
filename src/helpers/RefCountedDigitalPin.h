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

  void claim() {
    _claims++;
    if (_claims > 0) {
      digitalWrite(_pin, _active);
    }
  }
  void release() {
    _claims--;
    if (_claims == 0) {
      digitalWrite(_pin, !_active);
    }
  }
};
