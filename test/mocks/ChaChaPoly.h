#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Mock ChaChaPoly class for testing
// Provides minimal interface to allow Utils.cpp to compile (no real encryption)
class ChaChaPoly {
public:
  bool setKey(const uint8_t* key, size_t len) { return true; }
  bool setIV(const uint8_t* iv, size_t len) { return true; }
  void addAuthData(const void* data, size_t len) {}
  void encrypt(uint8_t* output, const uint8_t* input, size_t len) { memmove(output, input, len); }
  void decrypt(uint8_t* output, const uint8_t* input, size_t len) { memmove(output, input, len); }
  void computeTag(void* tag, size_t len) { memset(tag, 0, len); }
  bool checkTag(const void* tag, size_t len) { return true; }
  void clear() {}
};
