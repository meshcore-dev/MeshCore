#pragma once

#include <Arduino.h>

#define BUTTON_EVENT_NONE        0
#define BUTTON_EVENT_CLICK       1
#define BUTTON_EVENT_LONG_PRESS  2

class MomentaryButton {
  int8_t _pin;
  int8_t prev, cancel;
  bool _reverse, _pull;
  int _long_millis;
  int _threshold;  // analog mode
  unsigned long down_at;

  bool isPressed(int level) const;

public:
  MomentaryButton(int8_t pin, int long_press_mills=0, bool reverse=false, bool pulldownup=false);
  MomentaryButton(int8_t pin, int long_press_mills, int analog_threshold);
  void begin();
  int check(bool repeat_click=false);  // returns one of BUTTON_EVENT_*
  void cancelClick();  // suppress next BUTTON_EVENT_CLICK (if already in DOWN state)
  uint8_t getPin() { return _pin; }
  bool isPressed() const;
};
