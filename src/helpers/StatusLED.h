#pragma once

#include <Arduino.h>

#ifndef LED_ON_MILLIS
  #define LED_ON_MILLIS      20
#endif
#ifndef LED_ON_MSG_MILLIS
  #define LED_ON_MSG_MILLIS  200
#endif
#ifndef LED_CYCLE_MILLIS
  #define LED_CYCLE_MILLIS   4000
#endif
#ifndef LED_STATE_ON
  #define LED_STATE_ON       1
#endif

class StatusLED {
  uint8_t _pin;
  uint8_t _active;
  unsigned long _next_change = 0;
  unsigned long _last_on_duration = 0;
  uint8_t _state = 0;
  bool _alert = false;

public:
  StatusLED(uint8_t pin, uint8_t active = LED_STATE_ON) : _pin(pin), _active(active) { }

  void begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, _active ? LOW : HIGH);  // Start with LED off
  }

  void setAlert(bool alert) { _alert = alert; }
  bool isAlert() const { return _alert; }

  void loop() {
    unsigned long now = millis();
    if (now > _next_change) {
      if (_state == 0) {
        _state = 1;
        _last_on_duration = _alert ? LED_ON_MSG_MILLIS : LED_ON_MILLIS;
        _next_change = now + _last_on_duration;
      } else {
        _state = 0;
        _next_change = now + LED_CYCLE_MILLIS - _last_on_duration;
      }
      digitalWrite(_pin, (_state == _active) ? HIGH : LOW);
    }
  }
};
