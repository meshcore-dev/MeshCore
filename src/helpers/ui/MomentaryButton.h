#pragma once

#include <Arduino.h>

#define BUTTON_EVENT_NONE        0
#define BUTTON_EVENT_CLICK       1
#define BUTTON_EVENT_LONG_PRESS  2
#define BUTTON_EVENT_DOUBLE_CLICK 3
#define BUTTON_EVENT_TRIPLE_CLICK 4

class MomentaryButton {
  int8_t _pin;
  int8_t prev, cancel;
  bool _reverse, _pull;
  int _long_millis;
  int _threshold;  // analog mode
  unsigned long down_at;
  uint8_t _click_count;
  unsigned long _last_click_time;
  int _multi_click_window;
  bool _pending_click;

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
