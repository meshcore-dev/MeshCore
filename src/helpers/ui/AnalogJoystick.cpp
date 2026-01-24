#include "AnalogJoystick.h"

AnalogJoystick::AnalogJoystick(int8_t pin, JoyADCMapping *mappings, uint8_t num_mappings, int tolerance,
                               unsigned long debounce_ms) {
  _pin = pin;
  _mappings = mappings;
  _num_mappings = num_mappings;
  _tolerance = tolerance;
  _debounce_ms = debounce_ms;
  prev = 0;
  _last_change_time = 0;
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

  // Only return key if within tolerance
  if (closest_index >= 0 && min_diff < _tolerance) {
    return _mappings[closest_index].key_code;
  }
  return 0; // No valid key
}

uint8_t AnalogJoystick::check() {
  if (_pin < 0) return 0;

  int adc_value = analogRead(_pin);
  uint8_t key = findClosestKey(adc_value);

  if (key != prev) {
    unsigned long now = millis();
    if ((now - _last_change_time) > _debounce_ms) {
      prev = key;
      _last_change_time = now;
      return key;
    }
  }

  return 0;
}