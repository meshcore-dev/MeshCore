#pragma once

#include <stdint.h>

// Wrap-safe check: has `now` reached or passed timestamp `t`?
// Handles uint32_t rollover correctly for intervals up to ~24 days.
inline bool millisHasPassed(uint32_t now, uint32_t t) {
  return (int32_t)(now - t) >= 0;
}

#ifdef ARDUINO
#include <Arduino.h>
// Convenience wrapper using Arduino's millis().
inline bool millisHasPassed(uint32_t t) {
  return millisHasPassed((uint32_t)millis(), t);
}
#endif
