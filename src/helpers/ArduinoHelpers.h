#pragma once

#include <Mesh.h>
#include <Arduino.h>

/**
 * Monotonic elapsed-time accumulator that is wrap-safe on every platform.
 *
 * On the Adafruit nRF52 core, millis() is tick2ms(xTaskGetTickCount()): a 32-bit
 * FreeRTOS tick counter at configTICK_RATE_HZ (1024 Hz) scaled by 1000/1024. So
 * millis() wraps at 2^32 * 1000 / 1024 = 4,194,304,000 ms (48.544 days), NOT at
 * 2^32 ms. A uint32 subtraction across that wrap yields a phantom
 * 2^32 - 4,194,304,000 = 100,663,296 ms (27 h 57 m 43 s) of elapsed time.
 * Measuring in raw ticks, which really do wrap at 2^32, is exact.
 */
class MonotonicMillis {
  uint64_t _total;   // ticks on nRF52, milliseconds elsewhere
#if defined(NRF52_PLATFORM)
  uint32_t _prev;
#else
  unsigned long _prev;
#endif
public:
  MonotonicMillis() { reset(); }

  void reset() {
    _total = 0;
#if defined(NRF52_PLATFORM)
    _prev = xTaskGetTickCount();
#else
    _prev = millis();
#endif
  }

  void update() {
#if defined(NRF52_PLATFORM)
    uint32_t now = xTaskGetTickCount();
#else
    uint32_t now = millis();
#endif
    _total += (uint32_t)(now - _prev);   // both counters wrap at exactly 2^32
    _prev = now;
  }

  uint64_t getMillis() const {
#if defined(NRF52_PLATFORM)
    return (_total * 1000) / configTICK_RATE_HZ;
#else
    return _total;
#endif
  }
};

class VolatileRTCClock : public mesh::RTCClock {
  uint32_t base_time;
  MonotonicMillis elapsed;
public:
  VolatileRTCClock() { base_time = 1715770351; }   // 15 May 2024, 8:50pm
  uint32_t getCurrentTime() override { return base_time + elapsed.getMillis() / 1000; }
  void setCurrentTime(uint32_t time) override { base_time = time; elapsed.reset(); }

  void tick() override { elapsed.update(); }
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
