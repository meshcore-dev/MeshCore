#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Mock SHA256 class for native testing.
// Provides a deterministic stand-in so code can hash data without Arduino crypto deps.
class SHA256 {
  uint32_t state_ = 2166136261u;

public:
  void update(const void* data, size_t len) {
    update(static_cast<const uint8_t*>(data), len);
  }

  void update(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      state_ ^= data[i];
      state_ *= 16777619u;
      state_ += 0x9E3779B9u;
    }
  }

  void finalize(uint8_t* hash, size_t hashLen) {
    uint32_t value = state_;
    for (size_t i = 0; i < hashLen; ++i) {
      value ^= value >> 13;
      value *= 1274126177u;
      hash[i] = static_cast<uint8_t>((value >> ((i & 3) * 8)) & 0xFF);
    }
  }

  void resetHMAC(const uint8_t* key, size_t keyLen) {
    state_ = 2166136261u;
    update(key, keyLen);
    state_ ^= 0x36363636u;
  }

  void finalizeHMAC(const uint8_t* key, size_t keyLen, uint8_t* hash, size_t hashLen) {
    update(key, keyLen);
    finalize(hash, hashLen);
  }
};
