#pragma once

#include <stdint.h>

class RateLimiter {
  uint32_t _start;
  uint16_t _secs;
  uint16_t _maximum;
  uint16_t _count;

public:
  RateLimiter(uint16_t maximum, uint16_t secs)
      : _start(0), _secs(secs), _maximum(maximum), _count(0) {}

  bool allow(uint32_t now) {
    if (now - _start >= _secs) {
      _start = now;
      _count = 0;
    }

    if (_count >= _maximum)
      return false;
    
    _count++;

    return true;
  }
};

class AdaptiveRateLimiter {
  enum {
    EWMA_SMOOTHING = 3,
    EWMA_TOTAL_WEIGHT = EWMA_SMOOTHING + 1,
    EWMA_GROWTH_CAP = 4
  };

  static_assert(EWMA_SMOOTHING >= 1 && EWMA_SMOOTHING <= 256, "EWMA_SMOOTHING must be 1-256");
  static_assert(EWMA_GROWTH_CAP >= 1, "EWMA_GROWTH_CAP must be at least 1");

  uint32_t _start;
  uint16_t _secs;
  uint8_t _count;
  uint8_t _limit;
  uint8_t _ewma;
  uint8_t _burst;
  uint8_t _floor;

  static uint8_t clampU8(uint16_t v) { return v > 255 ? 255 : (uint8_t)v; }

  uint8_t nextEwma() const {
    uint8_t cap = clampU8((uint16_t)_ewma + EWMA_GROWTH_CAP);
    uint8_t effective = (_count > _ewma) ? cap : _count;
    uint16_t next = (uint16_t)_ewma * EWMA_SMOOTHING + effective;

    return (uint8_t)(next / EWMA_TOTAL_WEIGHT);
  }

  uint8_t computeLimit() const {
    uint8_t clamped = clampU8((uint16_t)_ewma * _burst);
    return clamped > _floor ? clamped : _floor;
  }

  void advanceWindow(uint32_t now) {
    if (now - _start < _secs)
      return;

    uint32_t elapsed = (_secs == 0) ? 1 : (now - _start) / _secs;

    if (elapsed > EWMA_TOTAL_WEIGHT * 8)
      elapsed = EWMA_TOTAL_WEIGHT * 8;

    _ewma = nextEwma();
    _limit = computeLimit();

    while (elapsed > 1 && _ewma > 0) {
      _count = 0;
      _ewma = nextEwma();
      _limit = computeLimit();

      elapsed--;
    }

    _start = now;
    _count = 0;
  }

public:
  AdaptiveRateLimiter(uint16_t secs, uint8_t burst, uint8_t floor)
      : _start(0), _secs(secs), _count(0), _limit(floor), _ewma(floor),
        _burst(burst), _floor(floor) {}

  bool allow(uint32_t now) {
    advanceWindow(now);

    if (_count >= _limit)
      return false;

    _count++;
    
    return true;
  }
};
