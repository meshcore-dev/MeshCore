#pragma once

#include <Arduino.h>
#include "RotaryInput.h"

#define BUTTON_EVENT_UP 5
#define BUTTON_EVENT_DOWN 6

class RotaryInputGPIO: public RotaryInput {
  int8_t _pin_a; 
  bool _pull_a;
  int8_t _pin_b;
  bool _pull_b;
  bool b_prec;
public:
  RotaryInputGPIO(int8_t pin_a, int8_t pin_b, bool pull_a=false, bool pull_b=false): _pin_a(pin_a), _pin_b(pin_b), _pull_a(pull_a), _pull_b(pull_b) {}

  bool begin() override;
  RotaryInputEvent poll() override;
  bool isReady() const override { return true;}
};
