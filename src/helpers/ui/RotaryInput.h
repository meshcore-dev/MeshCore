#pragma once

#include <Arduino.h>

enum class RotaryInputEvent : uint8_t {
  None,
  Next,
  Prev,
};

class RotaryInput {
public:
  virtual ~RotaryInput() = default;

  virtual bool begin() = 0;
  virtual RotaryInputEvent poll() = 0;
  virtual bool isReady() const = 0;
};
