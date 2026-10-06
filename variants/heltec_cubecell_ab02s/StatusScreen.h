#pragma once
#include <stdint.h>
#include <string.h>
#include "Oled.h"
#include <helpers/asr650x/StatusLines.h>
#include <helpers/asr650x/Button.h>

// One status screen. Auto-off after ON_MS, USER key wakes it. Redraws once a second, ONE page per loop() call
// (about 3-4 ms of I2C each) so serial frames and radio events are never starved.
class StatusScreen {
  OledPanel* _oled;
  asr650x::Button _button;
  asr650x::StatusInput _in;
  uint8_t _page[128];
  bool _on;
  uint32_t _on_since, _last_refresh;
  int8_t _next_page;              // -1 = idle, 0..7 = pages still to draw
  uint32_t _last_batt_ms;
  uint16_t _batt_mv;
  bool _error;
public:
  static const uint32_t ON_MS = 30000;
  StatusScreen() : _oled(0), _on(false), _on_since(0), _last_refresh(0), _next_page(-1), _last_batt_ms(0), _batt_mv(0), _error(false) {
    memset(&_in, 0, sizeof(_in));
  }
  void begin(OledPanel* oled);
  void set_input(const asr650x::StatusInput& in) { _in = in; _in.batt_mv = _batt_mv; }
  void wake(uint32_t now_ms);
  void loop(uint32_t now_ms);
  bool is_on() const { return _on; }
  bool failed() const { return _error; }
};
