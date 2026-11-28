#pragma once
#include <stdint.h>

namespace asr650x {

// Debounced press detector for the USER key (active LOW, shared with the battery divider control).
class Button {
  static const uint32_t DEBOUNCE_MS = 30;
  bool _down;  // raw level seen at the last update
  bool _armed; // a press may start (the key was seen released after the last hold-off/press)
  bool _fired;
  uint32_t _t_down;
  uint32_t _holdoff_until;
  bool _holdoff_active;

  static bool before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; } // wrap-safe a < b

public:
  Button()
      : _down(false), _armed(true), _fired(false), _t_down(0), _holdoff_until(0), _holdoff_active(false) {}

  void holdoff(uint32_t now_ms, uint32_t ms) { // e.g. right after getBatteryVoltage() released the pin
    _holdoff_until = now_ms + ms;
    _holdoff_active = true;
    _armed = false; // anything pressed before/while held off must be released first
    _down = false;
    _fired = false;
  }

  // true exactly once per accepted press
  bool update(bool pressed, uint32_t now_ms) {
    if (_holdoff_active) {
      if (before(now_ms, _holdoff_until)) return false;
      _holdoff_active = false;
    }
    if (!pressed) {
      _armed = true;
      _down = false;
      _fired = false;
      return false;
    }
    if (!_armed) return false; // still the same (or a held-off) press
    if (!_down) {
      _down = true;
      _t_down = now_ms;
      return false;
    }
    if (!_fired && (uint32_t)(now_ms - _t_down) >= DEBOUNCE_MS) {
      _fired = true;
      _armed = false;
      return true;
    }
    return false;
  }
};

} // namespace asr650x
