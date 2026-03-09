#pragma once

#include <Mesh.h>
#include <Arduino.h>

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

/**
 * \brief  Wrap-safe millis deadline check, handling the 49-day millis() overflow.
 * \param  target  The deadline timestamp obtained from millis() + delay.
 * \returns true when the deadline has passed.
 * \note   Use this instead of \c millis()>=target which fails near the 32-bit wrap.
 *         Works via signed subtraction (2's complement).
 */
inline bool millis_passed(unsigned long target) {
  return (long)(millis() - target) > 0;
}

class StdRNG : public mesh::RNG {
public:
  void begin(long seed) { randomSeed(seed); }
  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = (::random(0, 256) & 0xFF);
    }
  }
};
