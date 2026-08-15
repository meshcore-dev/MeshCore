#include "AnalogJoystick.h"

AnalogJoystick::AnalogJoystick(int8_t pin, JoyADCMapping *mappings, uint8_t num_mappings,
                               uint8_t select_key_code, unsigned long long_press_ms, int tolerance,
                               unsigned long debounce_ms) {
  _pin = pin;
  _mappings = mappings;
  _num_mappings = num_mappings;
  _select_key = select_key_code;
  _long_press_ms = long_press_ms;
  _tolerance = tolerance;
  _debounce_ms = debounce_ms;
  prev = 0;
  _last_change_time = 0;
  _select_press_start = 0;
  _long_press_triggered = false;
}

void AnalogJoystick::begin() {
  if (_pin >= 0) {
    pinMode(_pin, INPUT);
  }
}

uint8_t AnalogJoystick::findClosestKey(int adc_value) const {
  int closest_index = -1;
  int min_diff = 32767;

  for (uint8_t i = 0; i < _num_mappings; i++) {
    int diff = abs(adc_value - _mappings[i].adc_value);
    if (diff < min_diff) {
      min_diff = diff;
      closest_index = i;
    }
  }

  if (closest_index >= 0 && min_diff < _tolerance) {
    return _mappings[closest_index].key_code;
  }
  return 0;
}

uint8_t AnalogJoystick::check() {
  if (_pin < 0) return 0;

  int adc_value = analogRead(_pin);
  uint8_t key = findClosestKey(adc_value);

  // Handle SELECT button with long press support
  if (key == _select_key) {
    if (_select_press_start == 0) {
      // SELECT just pressed - start tracking
      _select_press_start = millis();
      _long_press_triggered = false;
      prev = key;
    } else if (!_long_press_triggered && (millis() - _select_press_start) >= _long_press_ms) {
      // Long press threshold reached
      _long_press_triggered = true;
      return 0xFF; // Special code for long press (only sent once)
    }
    // Still holding, waiting for either release or long press
    return 0;

  } else if (prev == _select_key && _select_press_start > 0) {
    // SELECT was just released
    bool was_long_press = _long_press_triggered;
    _select_press_start = 0;
    _long_press_triggered = false;
    prev = key; // Update to new state (likely 0/idle)

    if (!was_long_press) {
      // Released before long press - this is a click
      _last_change_time = millis();
      return _select_key;
    }
    // Was long press, already handled, don't send click
    return 0;

  } else {
    // Not SELECT button - handle other directions with debouncing
    if (key != prev) {
      unsigned long now = millis();
      if ((now - _last_change_time) > _debounce_ms) {
        prev = key;
        _last_change_time = now;
        return key;
      }
    }
  }

  return 0;
}

bool AnalogJoystick::isLongPress() {
  return _long_press_triggered;
}

bool AnalogJoystick::isPressed() const {
  if (_pin < 0) return false;

  int adc_value = analogRead(_pin);
  uint8_t key = findClosestKey(adc_value);

  // Return true if any key is pressed (not idle/released)
  return key != 0;
}