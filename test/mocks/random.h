#pragma once

#if defined(__has_include_next)
  #if __has_include_next(<random.h>)
    #include_next <random.h>
  #endif
#endif

#ifdef __cplusplus

#include <cstddef>
#include <cstring>

#include "Utils.h"

// Fake random generator for native tests.
// Fills buffers with deterministic bytes so generated packets are repeatable.
class FakeRng final : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override {
    memset(dest, 0x5A, sz);
  }
};

#endif
