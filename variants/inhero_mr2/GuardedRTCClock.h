/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <helpers/AutoDiscoverRTCClock.h>

// Wraps AutoDiscoverRTCClock with a lock to avoid RTC I2C access during
// critical timer configuration; writes are deferred until unlocked.
class GuardedRTCClock : public mesh::RTCClock {
 public:
  explicit GuardedRTCClock(mesh::RTCClock& fallback)
      : _fallback(&fallback),
        _inner(fallback),
        _locked(false),
        _pending_time_valid(false),
        _pending_time(0) {}

  void begin(TwoWire& wire) { _inner.begin(wire); }

  void setLocked(bool locked) {
    _locked = locked;
    if (!_locked && _pending_time_valid) {
      _inner.setCurrentTime(_pending_time);
      _pending_time_valid = false;
    }
  }

  uint32_t getCurrentTime() override {
    if (_locked) {
      return _fallback->getCurrentTime();
    }
    return _inner.getCurrentTime();
  }

  void setCurrentTime(uint32_t time) override {
    if (_locked) {
      _pending_time = time;
      _pending_time_valid = true;
      return;
    }
    _inner.setCurrentTime(time);
  }

  void tick() override { _inner.tick(); }

 private:
  mesh::RTCClock* _fallback;
  AutoDiscoverRTCClock _inner;
  volatile bool _locked;
  bool _pending_time_valid;
  uint32_t _pending_time;
};
