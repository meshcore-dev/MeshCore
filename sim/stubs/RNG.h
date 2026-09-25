#pragma once
// Stub for rweather Crypto library's RNG global — used by Ed25519::generatePrivateKey.
#include <stdint.h>
#include <stddef.h>

class RNGClass {
public:
  void begin(const char* tag);
  void rand(uint8_t* buf, size_t len);
  void loop();
};

extern RNGClass RNG;
