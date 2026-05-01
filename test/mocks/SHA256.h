#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock SHA256 class for native testing.
// Provides a fixed stand-in so code can compile without Arduino crypto deps.
// update() and resetHMAC() ignore all input; finalize() and finalizeHMAC()
// fill the requested output buffer with 0x11 bytes.
class SHA256 {
public:
  void update(const void*, size_t) {}

  void update(const uint8_t*, size_t) {}

  void finalize(uint8_t* hash, size_t hashLen) {
    for (size_t i = 0; i < hashLen; ++i) {
      hash[i] = 0x11;
    }
  }

  void resetHMAC(const uint8_t*, size_t) {}

  void finalizeHMAC(const uint8_t*, size_t, uint8_t* hash, size_t hashLen) {
    finalize(hash, hashLen);
  }
};
