#pragma once
#include <stdint.h>

namespace asr650x {

enum GpsState { GPS_OFF = 0, GPS_WARMING = 1, GPS_DONE = 2 };

// Decides when the GPS module should be powered. Pure logic: time comes in as seconds from the caller.
class GpsPolicy {
  bool _enabled;
  uint32_t _interval, _timeout;
  GpsState _state;
  uint32_t _t_start, _t_done;
  bool _timed_out;
  uint32_t _fixes;

  // elapsed seconds modulo 2^32 (safe across the counter wrap); a "negative" difference means the clock went
  // backwards and counts as 0
  static uint32_t since(uint32_t now, uint32_t then) {
    uint32_t d = now - then;
    return (int32_t)d < 0 ? 0 : d;
  }

public:
  GpsPolicy() : _enabled(false), _interval(0), _timeout(300), _state(GPS_OFF), _t_start(0), _t_done(0),
                _timed_out(false), _fixes(0) {}

  void configure(bool enabled, uint32_t interval_s, uint32_t fix_timeout_s) {
    _interval = interval_s;
    _timeout = fix_timeout_s;
    if (!enabled) { _enabled = false; _state = GPS_OFF; return; }
    _enabled = true;
  }

  void begin(uint32_t now_s) {
    if (_enabled) { _state = GPS_WARMING; _t_start = now_s; _timed_out = false; }
  }

  void request(uint32_t now_s) {                // the app asked for a fresh position
    _enabled = true;
    _state = GPS_WARMING;
    _t_start = now_s;
    _timed_out = false;
  }

  void tick(uint32_t now_s, bool fix_ok) {
    if (!_enabled) return;
    if (_state == GPS_WARMING) {
      if (fix_ok) { _state = GPS_DONE; _t_done = now_s; _fixes++; _timed_out = false; }
      else if (since(now_s, _t_start) >= _timeout) { _state = GPS_DONE; _t_done = now_s; _timed_out = true; }
    } else if (_state == GPS_DONE) {
      if (_interval > 0 && since(now_s, _t_done) >= _interval) { _state = GPS_WARMING; _t_start = now_s; }
    }
  }

  bool power_wanted() const { return _state == GPS_WARMING; }
  GpsState state() const { return _state; }
  bool timed_out() const { return _timed_out; }
  uint32_t fix_count() const { return _fixes; }
  uint32_t warming_for(uint32_t now_s) const { return _state == GPS_WARMING ? since(now_s, _t_start) : 0; }
};

}  // namespace asr650x
