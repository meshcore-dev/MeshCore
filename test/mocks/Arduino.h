#pragma once

#include <cstdint>
#include <cmath>
#include <ctype.h>  // Arduino.h provides the C character classification functions
#include "Stream.h"

inline uint32_t g_mock_millis = 0;

using std::isnan;

inline uint32_t millis() {
  return g_mock_millis;
}

inline void delay(uint32_t ms) {
  g_mock_millis += ms;
}

// Busy-wait loops call yield(); let mock time move so they can time out.
inline void yield() {
  g_mock_millis += 1;
}

// Minimal digital I/O so headers that drive enable/reset pins compile natively.
constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t INPUT = 0;
constexpr uint8_t OUTPUT = 1;

inline uint8_t g_mock_pin_levels[64] = {};

inline void pinMode(uint8_t, uint8_t) {}

inline void digitalWrite(uint8_t pin, uint8_t level) {
  if (pin < 64) g_mock_pin_levels[pin] = level;
}

inline int digitalRead(uint8_t pin) {
  return pin < 64 ? g_mock_pin_levels[pin] : LOW;
}
