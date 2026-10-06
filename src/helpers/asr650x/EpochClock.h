#pragma once
#include <stdint.h>

namespace asr650x {

// Wall clock = epoch + accumulated monotonic milliseconds. Lost on reset by design (spec §5).
// now() must be called at least once every ~49 days so a 32-bit millis() wrap is seen once.
class EpochClock {
  uint32_t _epoch;
  uint32_t _last_ms;
  uint64_t _rem_ms;   // sub-second remainder, < 1000 after each now()
  bool _synced;       // true once set() was called (by the app or by GPS)

public:
  explicit EpochClock(uint32_t epoch_at_boot = 1735689600u)   // 2025-01-01 until the app sets time
    : _epoch(epoch_at_boot), _last_ms(0), _rem_ms(0), _synced(false) {}

  uint32_t now(uint32_t ms) {
    uint32_t delta = ms - _last_ms;   // unsigned arithmetic handles one wrap
    _last_ms = ms;
    _rem_ms += delta;
    _epoch += (uint32_t)(_rem_ms / 1000);
    _rem_ms %= 1000;
    return _epoch;
  }

  void set(uint32_t epoch, uint32_t ms) {
    _epoch = epoch;
    _last_ms = ms;
    _rem_ms = 0;
    _synced = true;
  }

  bool synced() const { return _synced; }
};

}  // namespace asr650x
