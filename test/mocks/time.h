#pragma once

#if defined(__has_include_next)
  #if __has_include_next(<time.h>)
    #include_next <time.h>
  #endif
#endif

#ifdef __cplusplus

#include <cstdint>

#include "Dispatcher.h"

// Fake millisecond clock for native tests.
// Returns a stable timestamp so timer-dependent code stays deterministic.
class FakeMillis final : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override {
    return 0;
  }
};

// Fake RTC clock for native tests.
// Stores caller-controlled Unix time without depending on hardware RTC APIs.
class FakeRtc final : public mesh::RTCClock {
public:
  explicit FakeRtc(uint32_t initial_time) : _time(initial_time) {}

  uint32_t getCurrentTime() override {
    return _time;
  }

  void setCurrentTime(uint32_t time) override {
    _time = time;
  }

private:
  uint32_t _time;
};

#endif
