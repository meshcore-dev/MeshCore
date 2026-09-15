#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock AES128 class for testing
// Provides minimal interface to allow Utils.cpp to compile
class AES128 {
public:
  void setKey(const uint8_t* key, size_t keySize) {}
  void encryptBlock(uint8_t* output, const uint8_t* input) {}
  void decryptBlock(uint8_t* output, const uint8_t* input) {}
};
