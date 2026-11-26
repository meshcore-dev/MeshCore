#pragma once

#include <stdint.h>

// One radio ISR publishes; its owning loop consumes. Aligned word loads/stores
// are sufficient even on Cortex-M0, where an atomic exchange needs a lock. A
// mode transition never writes the producer's generation, so an IRQ arriving
// during claim remains pending. Hardware IRQ handlers do not reenter themselves.
class RadioInterruptEvents {
  uint32_t _published = 0;
  uint32_t _consumed = 0;

public:
  __attribute__((always_inline)) inline void record() {
    const uint32_t next = __atomic_load_n(&_published, __ATOMIC_RELAXED) + 1;
    __atomic_store_n(&_published, next, __ATOMIC_RELEASE);
  }
  bool pending() const {
    return __atomic_load_n(&_published, __ATOMIC_ACQUIRE) != _consumed;
  }
  bool claim() {
    const uint32_t current = __atomic_load_n(&_published, __ATOMIC_ACQUIRE);
    const bool ready = current != _consumed;
    _consumed = current;
    return ready;
  }
};
