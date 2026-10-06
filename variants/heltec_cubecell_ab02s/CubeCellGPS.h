#pragma once
#include <stdint.h>
#include <helpers/asr650x/NmeaParser.h>
#include <helpers/asr650x/GpsPolicy.h>

// Air530 on Serial1 (9600), power on GPIO14 (LOW = on). Serial1.begin() is called ONCE (each call leaks a 256 B buffer);
// the module is switched with the power pin only. Never prints to Serial (the protocol port).
class CubeCellGPS {
  asr650x::NmeaParser _nmea;
  asr650x::GpsPolicy _policy;
  bool _enabled;
  uint32_t _interval;
  bool _powered;
  bool _began;
  uint32_t _power_on_ms;
  uint8_t _cfg_sent;
  uint32_t _uptime_s, _uptime_ms_mark;      // monotonic seconds (wraps at 2^32 s, not at the millis() wrap)
  uint32_t uptime(uint32_t now_ms);
  uint32_t _last_tick_ms;
  void power(bool on, uint32_t now_ms);
  void on_fix(bool from_rmc);
public:
  CubeCellGPS();
  void begin();
  void loop(uint32_t now_ms);
  void apply(bool enabled, uint32_t interval_s);
  void request();
  bool enabled() const { return _enabled; }
  uint32_t interval() const { return _interval; }
  bool powered() const { return _powered; }
  asr650x::GpsState state() const { return _policy.state(); }
  bool timed_out() const { return _policy.timed_out(); }
  const asr650x::GpsData& data() const { return _nmea.data(); }
  uint32_t sentences_ok() const { return _nmea.sentences_ok(); }
  uint32_t warming_s() const { return _policy.warming_for(_uptime_s); }
};

extern CubeCellGPS cubecell_gps;
