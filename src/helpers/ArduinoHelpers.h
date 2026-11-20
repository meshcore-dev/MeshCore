#pragma once

#include <Mesh.h>
#include <MeshCore.h>
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

class StdRNG : public mesh::RNG {
public:
  void begin(long seed) { randomSeed(seed); }
  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = (::random(0, 256) & 0xFF);
    }
  }
};

// Returns true for dirty resets (power-on, watchdog, brownout, panic).
// Returns false for clean wakes (deep sleep, software restart).
inline bool wasDirtyReset(mesh::MainBoard& board) {
#if defined(ESP32)
  esp_reset_reason_t rst = esp_reset_reason();
  return (rst != ESP_RST_DEEPSLEEP && rst != ESP_RST_SW);
#elif defined(NRF52_PLATFORM)
  return !(board.getResetReason() & POWER_RESETREAS_SREQ_Msk);
#else
  (void)board;
  return true;
#endif
}
