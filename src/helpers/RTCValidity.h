#pragma once

#include <stdint.h>

namespace mesh {

template <typename AdjustClock>
bool seedRTCIfLostPower(bool lost_power, uint32_t fallback_time, AdjustClock adjust_clock) {
  if (!lost_power) return false;
  adjust_clock(fallback_time);
  return true;
}

}  // namespace mesh
