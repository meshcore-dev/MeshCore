#include "target.h"
#include <Utils.h>
#include <Identity.h>

CubeCellBoard board;
CubeCellRadio radio_driver;
CubeCellSensors sensors;
CubeCellRTC rtc_clock;

bool radio_init() {
  radio_driver.init();   // Radio.Init first: getRngSeed() (called next by main.cpp) needs the radio
  return true;
}

namespace {
class SeedRNG : public mesh::RNG {
  const uint8_t* _p;
public:
  explicit SeedRNG(const uint8_t* p) : _p(p) {}
  void random(uint8_t* dest, size_t sz) override { memcpy(dest, _p, sz); }
};

// Deterministic, device-unique, NOT secret (derived from the public chip ID): it only exists so the
// firmware can answer the app before a key is imported. It is never persisted and never transmits
// (TxGate), so the app must import a private key before the node can be used.
void placeholder_seed(uint8_t seed[32]) {
  uint64_t id = getID();
  uint8_t in[8 + 21];
  memcpy(in, &id, 8);
  memcpy(in + 8, "asr650x-unprovisioned", 21);
  mesh::Utils::sha256(seed, 32, in, sizeof(in));
}
}

mesh::LocalIdentity radio_new_identity() {
  uint8_t seed[32];
  placeholder_seed(seed);
  SeedRNG rng(seed);
  return mesh::LocalIdentity(&rng);
}

bool asr650x_identity_is_placeholder(const mesh::Identity& id) {
  mesh::LocalIdentity p = radio_new_identity();
  return id.matches(p);
}
