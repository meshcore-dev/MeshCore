#pragma once
#include <stdint.h>
#if __cplusplus >= 201402L
#define M1_BATT_CONSTEXPR constexpr
#else
#define M1_BATT_CONSTEXPR
#endif

// Independent from UI activity. Late loops take one sample, never catch up
// with multiple copies of the same voltage reading.
class ConsecutiveLowBattery {
  uint32_t last_sample_ms = 0;
  uint8_t low_count = 0;
public:
  static constexpr uint32_t interval_ms = 30000;
  static constexpr uint16_t threshold_mv = 3450;
  static constexpr uint8_t required_samples = 10;
  M1_BATT_CONSTEXPR void reset(uint32_t now) { last_sample_ms = now; low_count = 0; }
  constexpr uint8_t count() const { return low_count; }
  M1_BATT_CONSTEXPR bool update(uint32_t now, uint16_t mv, bool usb_present) {
    if (usb_present) { reset(now); return false; }
    if (uint32_t(now - last_sample_ms) < interval_ms) return false;
    // Keep the 30-second cadence anchored to reset(), including when a
    // screen refresh delays a loop. Consume just ONE real reading, and
    // skip missed slots instead of inventing samples or accumulating drift.
    last_sample_ms += ((uint32_t(now - last_sample_ms)) / interval_ms) * interval_ms;
    if (mv == 0 || mv >= threshold_mv) { low_count = 0; return false; }
    if (low_count < required_samples) ++low_count;
    return low_count == required_samples;
  }
};

// Emergency power-off monitor: two valid readings below 3.28V, spaced by
// ten seconds. It is intentionally independent of the 3.45V sleep monitor.
class CriticalLowBatteryShutdown {
  uint32_t last_sample_ms = 0;
  uint8_t low_count = 0;
  bool shutdown_requested = false;
public:
  static constexpr uint32_t interval_ms = 10000;
  static constexpr uint16_t threshold_mv = 3280;
  static constexpr uint8_t required_samples = 2;

  M1_BATT_CONSTEXPR void reset(uint32_t now) {
    last_sample_ms = now;
    low_count = 0;
    shutdown_requested = false;
  }
  constexpr uint8_t count() const { return low_count; }

  M1_BATT_CONSTEXPR bool update(uint32_t now, uint16_t mv, bool usb_present) {
    if (usb_present) { reset(now); return false; }
    if (mv == 0 || mv >= threshold_mv) {
      low_count = 0;
      last_sample_ms = now;
      return false;
    }
    if (shutdown_requested) return false;

    // The first valid low reading starts the interval immediately. Subsequent
    // samples must be at least ten seconds apart.
    if (low_count == 0) {
      last_sample_ms = now;
      low_count = 1;
      return false;
    }
    if (uint32_t(now - last_sample_ms) < interval_ms) return false;

    // One actual reading per call; skip missed periods instead of replaying
    // the same voltage several times after a blocked UI/display operation.
    last_sample_ms += ((uint32_t(now - last_sample_ms)) / interval_ms) * interval_ms;

    if (low_count < required_samples) ++low_count;
    if (low_count == required_samples) {
      shutdown_requested = true;
      return true;
    }
    return false;
  }
};
#undef M1_BATT_CONSTEXPR
