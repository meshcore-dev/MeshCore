// Stub implementation of rweather's RNGClass for host-side sim builds.
// Ed25519::generatePrivateKey() calls RNG.rand() — we satisfy it with /dev/urandom.
#include "RNG.h"
#include <cstdio>

RNGClass RNG;

void RNGClass::begin(const char*) {}

void RNGClass::rand(uint8_t* buf, size_t len) {
  FILE* f = fopen("/dev/urandom", "rb");
  if (f) {
    (void)fread(buf, 1, len, f);
    fclose(f);
  } else {
    // fallback: deterministic
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(i * 0x6B ^ 0xA3);
  }
}

void RNGClass::loop() {}
