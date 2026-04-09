#pragma once

#include <Mesh.h>
#include <Arduino.h>
#include <helpers/IdentityStore.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ascon.h>
#include <Utils.h>

class VolatileRTCClock : public mesh::RTCClock {
  uint32_t base_time;
  uint64_t accumulator;
  unsigned long prev_millis;
public:
  VolatileRTCClock() { base_time = 1715770351; accumulator = 0; prev_millis = millis(); }   // 15 May 2024, 8:50pm
  uint32_t getCurrentTime() override { return base_time + accumulator/1000; }
  void setCurrentTime(uint32_t time) override { base_time = time; accumulator = 0; prev_millis = millis(); }

  void tick() override {
    unsigned long now = millis();
    accumulator += (now - prev_millis);
    prev_millis = now;
  }
};

class ArduinoMillis : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override { return millis(); }
};

namespace mesh {
void initHardwareRNG();
void deinitHardwareRNG();
}

class AsconRNG : public mesh::RNG {
protected:
  RadioLibWrapper* _radio;
  FILESYSTEM* _fs;
  const char* _seed_path;
  ascon_state_t _xof;
  bool _is_ready;

  uint32_t getHardwareRandom32();
  void gatherEntropy(uint8_t* dest, size_t len);
  bool loadSeed(uint8_t* dest, size_t len);
  bool saveSeed();
  void initState(const uint8_t* seed, size_t len);

public:
  AsconRNG()
      : _radio(NULL), _fs(NULL), _seed_path("/seed.rng"), _is_ready(false) {
  }

  void begin();
  void reseed(const uint8_t* extra = NULL, size_t extra_len = 0);
  void setRadioEntropySource(RadioLibWrapper& radio) { _radio = &radio; }
  void attachPersistence(FILESYSTEM& fs, const char* seed_path = "/seed.rng");
  void random(uint8_t* dest, size_t sz) override {
    if (!_is_ready) {
      begin();
    }
    ascon_squeeze(&_xof, dest, sz);
  }
};

class StdRNG : public AsconRNG {
public:
  void begin() { AsconRNG::begin(); }
  void begin(long seed) {
    if (!_is_ready) {
      AsconRNG::begin();
    }
    ascon_absorb(&_xof, (const uint8_t*)&seed, sizeof(seed));
  }
};
