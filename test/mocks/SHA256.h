#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock SHA256 class for testing
// Provides minimal interface to allow Utils.cpp to compile
class SHA256 {
public:
  void update(const uint8_t* data, size_t len) {}
  void finalize(uint8_t* hash, size_t hashLen) {}
  void resetHMAC(const uint8_t* key, size_t keyLen) {}
  void finalizeHMAC(const uint8_t* key, size_t keyLen, uint8_t* hash, size_t hashLen) {}
};
