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

struct AdaptiveRateLimiterStats {
  uint8_t limit;
  uint8_t remaining;
  uint8_t denied;
  uint8_t load_avg;
  uint16_t limit_reached;
  uint32_t last_limit_reached_ago;
};

class AdaptiveRateLimiter {
  // EWMA of recent per-window advert counts; cap each window at max(floor, ewma * burst).
  enum {
    EWMA_SMOOTHING = 3,
    EWMA_TOTAL_WEIGHT = EWMA_SMOOTHING + 1,
    EWMA_GROWTH_CAP = 4
  };

  static_assert(EWMA_SMOOTHING >= 1 && EWMA_SMOOTHING <= 256, "EWMA_SMOOTHING must be 1-256");
  static_assert(EWMA_GROWTH_CAP >= 1, "EWMA_GROWTH_CAP must be at least 1");

  uint32_t _window_start;
  uint16_t _window_secs;
  uint8_t _window_count;
  uint8_t _window_limit;
  uint8_t _load_avg;
  uint8_t _burst_multiplier;
  uint8_t _min_limit;
  uint8_t _denied;
  uint16_t _limit_reached;
  uint32_t _last_limit_reached_at;

  static uint8_t clampU8(uint16_t v) { return v > 255 ? 255 : (uint8_t)v; }

  uint8_t nextEwma() const {
    uint8_t cap = clampU8((uint16_t)_load_avg + EWMA_GROWTH_CAP);
    uint8_t effective = (_window_count > _load_avg) ? cap : _window_count;
    uint16_t next = (uint16_t)_load_avg * EWMA_SMOOTHING + effective;

    return (uint8_t)(next / EWMA_TOTAL_WEIGHT);
  }

  uint8_t computeLimit() const {
    uint8_t clamped = clampU8((uint16_t)_load_avg * _burst_multiplier);
    return clamped > _min_limit ? clamped : _min_limit;
  }

  void advanceWindow(uint32_t now) {
    if (now - _window_start < _window_secs)
      return;

    uint32_t elapsed = (_window_secs == 0) ? 1 : (now - _window_start) / _window_secs;

    if (elapsed > EWMA_TOTAL_WEIGHT * 8)
      elapsed = EWMA_TOTAL_WEIGHT * 8;

    _load_avg = nextEwma();
    _window_limit = computeLimit();

    while (elapsed > 1 && _load_avg > 0) {
      _window_count = 0;
      _load_avg = nextEwma();
      _window_limit = computeLimit();

      elapsed--;
    }

    _window_start = now;
    _window_count = 0;
    _denied = 0;
  }

public:
  AdaptiveRateLimiter(uint16_t window_secs, uint8_t burst_multiplier, uint8_t min_limit)
      : _window_start(0), _window_secs(window_secs), _window_count(0), _window_limit(min_limit), _load_avg(min_limit),
        _burst_multiplier(burst_multiplier), _min_limit(min_limit), _denied(0), _limit_reached(0), _last_limit_reached_at(0) {}

  bool allow(uint32_t now) {
    advanceWindow(now);

    if (_window_count >= _window_limit) {
      if (_denied < 255) _denied++;
      return false;
    }

    _window_count++;

    if (_window_count >= _window_limit) {
      if (_limit_reached < 65535) _limit_reached++;
      _last_limit_reached_at = now;
    }

    return true;
  }

  void clearStats() {
    _denied = 0;
    _limit_reached = 0;
    _last_limit_reached_at = 0;
  }

  AdaptiveRateLimiterStats stats(uint32_t now) {
    advanceWindow(now);
    uint8_t remaining = (_window_count < _window_limit) ? (_window_limit - _window_count) : 0;
    uint32_t last_limit_reached_ago = (_last_limit_reached_at == 0) ? 0 : (now - _last_limit_reached_at);
    return { _window_limit, remaining, _denied, _load_avg, _limit_reached, last_limit_reached_ago };
  }
};
