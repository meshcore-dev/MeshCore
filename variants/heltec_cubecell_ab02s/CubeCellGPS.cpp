#include "CubeCellGPS.h"
#include "target.h"
#include <Arduino.h>

CubeCellGPS cubecell_gps;

static const uint32_t FIX_TIMEOUT_S = 300;                 // spec: wait at most 5 minutes for a fix

CubeCellGPS::CubeCellGPS() : _enabled(true), _interval(0), _powered(false), _began(false), _power_on_ms(0), _cfg_sent(0), _uptime_s(0), _uptime_ms_mark(0), _last_tick_ms(0) {}

uint32_t CubeCellGPS::uptime(uint32_t now_ms) {
  uint32_t d = now_ms - _uptime_ms_mark;                    // unsigned: correct across the millis() wrap
  _uptime_s += d / 1000;
  _uptime_ms_mark += (d / 1000) * 1000;
  return _uptime_s;
}

void CubeCellGPS::power(bool on, uint32_t now_ms) {
  if (on == _powered) return;
  digitalWrite(GPIO14, on ? LOW : HIGH);                    // VGPS_Control: LOW = powered
  _powered = on;
  if (on) { _power_on_ms = now_ms; _cfg_sent = 0; _nmea.reset_fix(); }
}

void CubeCellGPS::begin() {
  pinMode(GPIO14, OUTPUT);
  digitalWrite(GPIO14, HIGH);                               // off
  Serial1.begin(9600);                                      // once
  _began = true;
  _policy.configure(_enabled, _interval, FIX_TIMEOUT_S);
  _uptime_ms_mark = millis();
  _policy.begin(uptime(millis()));
}

void CubeCellGPS::apply(bool enabled, uint32_t interval_s) {
  bool turned_on = enabled && !_enabled;
  _enabled = enabled;
  _interval = interval_s > 86400 ? 86400 : interval_s;
  _policy.configure(_enabled, _interval, FIX_TIMEOUT_S);
  if (turned_on && _began) _policy.request(uptime(millis()));
}

void CubeCellGPS::request() {
  if (_enabled && _began) _policy.request(uptime(millis()));
}

void CubeCellGPS::on_fix(bool from_rmc) {
  const asr650x::GpsData& d = _nmea.data();
  if (d.pos_valid) {
    sensors.node_lat = d.lat_e6 / 1000000.0;
    sensors.node_lon = d.lon_e6 / 1000000.0;
    sensors.node_altitude = d.alt_dm / 10.0;
  }
  if (from_rmc && d.rmc_valid) {                            // time only from the RMC just parsed, never from a stale one
    uint32_t epoch = asr650x::gps_to_epoch(d.date_ddmmyy, d.time_hhmmss);
    if (epoch != 0) {
      uint32_t now = rtc_clock.getCurrentTime();
      uint32_t diff = epoch > now ? epoch - now : now - epoch;
      if (!rtc_clock.synced() || diff >= 2) rtc_clock.setCurrentTime(epoch);
    }
  }
}

void CubeCellGPS::loop(uint32_t now_ms) {
  if (!_began) return;
  while (Serial1.available() > 0) {                         // always drain: the UART buffer must not fill up
    int c = Serial1.read();
    if (c < 0) break;
    if (!_powered) continue;
    asr650x::NmeaEvent e = _nmea.feed((uint8_t)c);
    if (e == asr650x::NMEA_GGA || e == asr650x::NMEA_RMC) {
      const asr650x::GpsData& d = _nmea.data();
      if (d.pos_valid) on_fix(e == asr650x::NMEA_RMC);
    }
  }
  // Air530Z: $PCAS03 keeps only GGA and RMC (the Air530 PGKC242 command is ignored by this module). Sent at 1 s
  // and again at 3 s after power-on, in case the module was not listening yet the first time.
  if (_powered && _cfg_sent < 2 && now_ms - _power_on_ms >= (_cfg_sent == 0 ? 1000u : 3000u)) {
    char line[64];
    int n = asr650x::nmea_make(line, sizeof(line), "PCAS03,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0");
    if (n > 0) Serial1.write((const uint8_t*)line, (size_t)n);
    _cfg_sent++;
  }
  if (now_ms - _last_tick_ms >= 1000) {
    _last_tick_ms = now_ms;
    const asr650x::GpsData& d = _nmea.data();
    bool fix_ok = _powered && d.pos_valid && d.rmc_valid && asr650x::gps_to_epoch(d.date_ddmmyy, d.time_hhmmss) != 0;
    _policy.tick(uptime(now_ms), fix_ok);
    power(_policy.power_wanted(), now_ms);
  }
}
