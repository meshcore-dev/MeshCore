#pragma once

#include <Arduino.h>

class AnalogJoystick {
public:
  struct JoyADCMapping {
    int adc_value;
    uint8_t key_code;
  };

private:
  int8_t _pin;
  uint8_t prev;
  int _tolerance;
  unsigned long _debounce_ms;
  unsigned long _last_change_time; // Long press tracking
  uint8_t _select_key;
  unsigned long _select_press_start;
  bool _long_press_triggered;
  unsigned long _long_press_ms;

  JoyADCMapping *_mappings;
  uint8_t _num_mappings;

  uint8_t findClosestKey(int adc_value) const;

public:
  AnalogJoystick(int8_t pin, JoyADCMapping *mappings, uint8_t num_mappings, uint8_t select_key_code,
                 unsigned long long_press_ms = 1000, int tolerance = 300, unsigned long debounce_ms = 50);
  void begin();
  uint8_t check();
  bool isLongPress();
  bool isPressed() const;
  uint8_t getPin() const { return _pin; }
  uint8_t getCurrentKey() const { return prev; }
};