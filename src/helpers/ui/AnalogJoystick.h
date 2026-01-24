#pragma once

#include <Arduino.h>

class AnalogJoystick {
public:
  struct JoyADCMapping {
    int adc_value;
    uint8_t key_code; // ← Changed from int8_t to uint8_t
  };

private:
  int8_t _pin;
  uint8_t prev; // ← Changed from int8_t to uint8_t
  int _tolerance;
  unsigned long _debounce_ms;
  unsigned long _last_change_time;

  JoyADCMapping *_mappings;
  uint8_t _num_mappings;

  uint8_t findClosestKey(int adc_value) const; // ← Changed return type

public:
  AnalogJoystick(int8_t pin, JoyADCMapping *mappings, uint8_t num_mappings, int tolerance = 300,
                 unsigned long debounce_ms = 50);
  void begin();
  uint8_t check(); // ← Changed return type from int8_t to uint8_t
  uint8_t getPin() const { return _pin; }
  uint8_t getCurrentKey() const { return prev; }
};