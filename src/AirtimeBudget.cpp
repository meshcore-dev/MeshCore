#include "AirtimeBudget.h"

namespace mesh {

void AirtimeBudget::clear() {
  _window_ms = 0;
  _slot_ms = 0;
  _limit_ms = 0;
  _used_ms = 0;
  _slot_start = 0;
  _slot_count = 0;
  _current = 0;
  for (uint8_t i = 0; i < MAX_SLOTS; i++) {
    _slots[i] = 0;
  }
}

void AirtimeBudget::begin(uint32_t window_ms, uint32_t limit_ms, uint32_t now, uint8_t slot_count) {
  clear();
  if (window_ms == 0 || slot_count == 0) return;   // ledger disabled
  if (slot_count > MAX_SLOTS) {
    slot_count = MAX_SLOTS;
  }

  _window_ms = window_ms;
  _limit_ms = limit_ms;
  _slot_count = slot_count;
  _slot_ms = (window_ms + slot_count - 1) / slot_count;   // round up, so the slots cover the whole window
  _slot_start = now;
}

void AirtimeBudget::update(uint32_t now) {
  if (_slot_count == 0) return;

  uint32_t elapsed = now - _slot_start;   // wraps correctly with millis()
  if (elapsed >= _window_ms) {   // everything is stale
    _used_ms = 0;
    _current = 0;
    for (uint8_t i = 0; i < _slot_count; i++) {
      _slots[i] = 0;
    }
    _slot_start = now;
    return;
  }

  uint32_t steps = elapsed / _slot_ms;
  for (uint32_t s = 0; s < steps; s++) {
    _current = (_current + 1) % _slot_count;
    _used_ms -= _slots[_current];
    _slots[_current] = 0;
  }
  _slot_start += steps * _slot_ms;
}

bool AirtimeBudget::canSpend(uint32_t airtime_ms, uint32_t now) {
  if (!isEnabled()) return true;
  update(now);
  return _used_ms + airtime_ms <= _limit_ms;
}

void AirtimeBudget::record(uint32_t airtime_ms, uint32_t now) {
  if (_slot_count == 0 || airtime_ms == 0) return;
  update(now);
  _slots[_current] += airtime_ms;
  _used_ms += airtime_ms;
}

}
