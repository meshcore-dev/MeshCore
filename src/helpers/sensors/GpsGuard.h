#pragma once

// GPS anti-spoofing guard (opt-in with GPS_GUARD).
//
// A location provider asks the guard before it stores a fix as its last fix and before it
// writes GPS time to the clock:
//  - acceptFix(): plausibility checks (satellites, HDOP, optional build-time region, distance
//    from a reference position, jump speed) and N consecutive consistent fixes;
//  - acceptTime(): GPS time only while a trusted fix is current, not before this firmware
//    was built, and once the clock is valid only small corrections (clock drift after the
//    first accepted sync), so a spoofer cannot walk the clock away.
// A trusted fix that keeps disagreeing with the clock by the same offset raises an alert;
// the operator can then accept that GPS time (acceptAlertedTime()).

#include <Arduino.h>
#include <MeshCore.h>
#include <RTClib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef GPS_GUARD_MIN_SATS
  #define GPS_GUARD_MIN_SATS        4
#endif
#ifndef GPS_GUARD_MAX_HDOP10
  #define GPS_GUARD_MAX_HDOP10      40      // HDOP 4.0
#endif
#ifndef GPS_GUARD_FENCE_M
  #define GPS_GUARD_FENCE_M         1000    // max distance from the reference position (0 = off)
#endif
#ifndef GPS_GUARD_STABLE_FIXES
  #define GPS_GUARD_STABLE_FIXES    5       // consecutive consistent fixes (one per GPS second)
#endif
#ifndef GPS_GUARD_STABLE_M
  #define GPS_GUARD_STABLE_M        100
#endif
#ifndef GPS_GUARD_MAX_SPEED_KMH
  #define GPS_GUARD_MAX_SPEED_KMH   250     // jump limit relative to the last trusted fix
#endif
#ifndef GPS_GUARD_TIME_MAX_STEP
  #define GPS_GUARD_TIME_MAX_STEP   600     // max GPS-vs-clock correction (s) once the clock is valid
#endif
#ifndef GPS_GUARD_TIME_DRIFT_BASE
  #define GPS_GUARD_TIME_DRIFT_BASE 5       // after the first sync: fixed allowance (s) ...
#endif
#ifndef GPS_GUARD_TIME_DRIFT_PPM
  #define GPS_GUARD_TIME_DRIFT_PPM  100     // ... plus clock drift since that sync (~8.6 s/day)
#endif
#ifndef GPS_GUARD_ALERT_MIN
  #define GPS_GUARD_ALERT_MIN       60      // alert after GPS disagrees with the clock this long ...
#endif
#ifndef GPS_GUARD_ALERT_TOLERANCE
  #define GPS_GUARD_ALERT_TOLERANCE 60      // ... by an offset that stays within this many seconds
#endif
#ifndef GPS_GUARD_TRUSTED_MS
  #define GPS_GUARD_TRUSTED_MS      5000    // a trusted fix is "current" for this long
#endif

class GpsGuard {
  bool     _enabled = true;               // off: every fix and GPS time passes, as without GPS_GUARD
  double   _ref_lat = 0, _ref_lon = 0;    // 0,0 = no reference position
  uint32_t _fence_m = GPS_GUARD_FENCE_M;

  bool     _have_trusted = false;
  unsigned long _last_trusted_ms = 0;
  double   _last_trusted_lat = 0, _last_trusted_lon = 0;
  double   _cand_lat = 0, _cand_lon = 0;
  uint32_t _cand_time = 0;                // GPS second of the last counted candidate
  uint32_t _rej_log_time = 0;             // GPS second of the last logged rejection
  uint8_t  _stable_count = 0;
  char     _fix_status[24] = "no fix";

  char     _time_status[24] = "-";
  bool     _time_accepted = false;        // a GPS time sync was accepted (drift budget anchor)
  unsigned long _last_time_accept_ms = 0;
  bool     _time_disagree = false;
  long     _time_disagree_offset = 0;     // GPS minus clock, seconds
  unsigned long _time_disagree_since_ms = 0;
  bool     _time_alert = false;

  static double distanceM(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0, D2R = 0.017453292519943295;
    double dlat = (lat2 - lat1) * D2R, dlon = (lon2 - lon1) * D2R;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * D2R) * cos(lat2 * D2R) * sin(dlon / 2) * sin(dlon / 2);
    return 2 * R * atan2(sqrt(a), sqrt(1 - a));
  }

  bool trustedFixIsCurrent() const {
    return _have_trusted && (uint32_t)(millis() - _last_trusted_ms) <= GPS_GUARD_TRUSTED_MS;
  }

  void trackTimeDisagreement(long offset) {
    const unsigned long now = millis();
    if (!_time_disagree || labs(offset - _time_disagree_offset) > GPS_GUARD_ALERT_TOLERANCE) {
      _time_disagree = true;
      _time_disagree_offset = offset;
      _time_disagree_since_ms = now;
      _time_alert = false;
      return;
    }
    if (!_time_alert && (uint32_t)(now - _time_disagree_since_ms) >= GPS_GUARD_ALERT_MIN * 60000UL) {
      _time_alert = true;
      MESH_DEBUG_PRINTLN("GPS TIME ALERT: GPS differs from clock by %lds for %u min", offset, GPS_GUARD_ALERT_MIN);
    }
  }

public:
  // Plausible GPS time window: not before this firmware was built, at most 10 years after.
  static uint32_t minEpoch() {
    static const uint32_t min_epoch = DateTime(__DATE__, __TIME__).unixtime() - 86400;
    return min_epoch;
  }
  static uint32_t maxEpoch() { return minEpoch() + 10UL * 365 * 86400; }

  void setReference(double lat, double lon) { _ref_lat = lat; _ref_lon = lon; }
  double getRefLat() const { return _ref_lat; }
  double getRefLon() const { return _ref_lon; }
  void setFence(uint32_t metres) { _fence_m = metres; }
  uint32_t getFence() const { return _fence_m; }
  bool hasTrustedFix() const { return _have_trusted; }
  bool isEnabled() const { return _enabled; }
  void setEnabled(bool enabled) {
    if (enabled == _enabled) return;
    _enabled = enabled;
    // start over: a re-enabled guard needs fresh stable fixes, and no alert survives
    _have_trusted = false;
    _stable_count = 0;
    _time_accepted = false;
    _time_disagree = false;
    _time_alert = false;
    strcpy(_fix_status, "no fix");
    strcpy(_time_status, "-");
  }
  bool isTimeAlert() const { return _time_alert; }

  // Fix as reported by the receiver: lat/lon in micro-degrees, HDOP in tenths (255 = unknown),
  // gps_time = UTC epoch of the fix. Returns true if the fix can be stored as trusted.
  bool acceptFix(long lat_e6, long lon_e6, long sats, uint8_t hdop10, uint32_t gps_time) {
    const double lat = lat_e6 / 1000000.0;
    const double lon = lon_e6 / 1000000.0;
    const unsigned long now = millis();
    if (!_enabled) return true;

    const char* why = NULL;
    double dist = 0;
    if (sats < GPS_GUARD_MIN_SATS) {
      why = "sats";
    } else if (hdop10 == 255 || hdop10 > GPS_GUARD_MAX_HDOP10) {
      why = "hdop";
    }
#if defined(GPS_GUARD_LAT_MIN) && defined(GPS_GUARD_LAT_MAX) && defined(GPS_GUARD_LON_MIN) && defined(GPS_GUARD_LON_MAX)
    else if (lat < GPS_GUARD_LAT_MIN || lat > GPS_GUARD_LAT_MAX || lon < GPS_GUARD_LON_MIN || lon > GPS_GUARD_LON_MAX) {
      why = "region";
    }
#endif
    else if (_fence_m > 0 && (_ref_lat != 0 || _ref_lon != 0) &&
             (dist = distanceM(_ref_lat, _ref_lon, lat, lon)) > _fence_m) {
      why = "fence";
    } else if (_have_trusted) {
      dist = distanceM(_last_trusted_lat, _last_trusted_lon, lat, lon);
      double secs = (uint32_t)(now - _last_trusted_ms) / 1000.0;
      if (secs < 1) secs = 1;
      if (dist / secs > GPS_GUARD_MAX_SPEED_KMH / 3.6) why = "jump";
    }

    if (why) {
      _stable_count = 0;
      if (dist > 0) {
        snprintf(_fix_status, sizeof(_fix_status), "rej %s %.1fkm", why, dist / 1000.0);
      } else {
        snprintf(_fix_status, sizeof(_fix_status), "rej %s", why);
      }
      if (gps_time == 0 || gps_time != _rej_log_time) {  // GGA and RMC report the same second: log it once
        _rej_log_time = gps_time;
        MESH_DEBUG_PRINTLN("GPS fix rejected (%s): %f,%f sats=%ld hdop=%u", why, lat, lon, sats, hdop10);
      }
      return false;
    }

    if (!_have_trusted || _stable_count < GPS_GUARD_STABLE_FIXES) {
      // Require several consecutive, mutually consistent fixes. GGA and RMC report the
      // same GPS second: count each second once.
      if (_stable_count > 0 && distanceM(_cand_lat, _cand_lon, lat, lon) > GPS_GUARD_STABLE_M) {
        _stable_count = 0;
      }
      if (_stable_count == 0 || gps_time != _cand_time) {
        if (_stable_count < 255) _stable_count++;
      }
      _cand_lat = lat;
      _cand_lon = lon;
      _cand_time = gps_time;
      if (_stable_count < GPS_GUARD_STABLE_FIXES) {
        snprintf(_fix_status, sizeof(_fix_status), "settle %u/%u", _stable_count, GPS_GUARD_STABLE_FIXES);
        return false;
      }
    }

    if (!_have_trusted) {
      MESH_DEBUG_PRINTLN("GPS fix trusted: %f,%f sats=%ld hdop=%u", lat, lon, sats, hdop10);
    }
    _have_trusted = true;
    _last_trusted_ms = now;
    _last_trusted_lat = lat;
    _last_trusted_lon = lon;
    snprintf(_fix_status, sizeof(_fix_status), "ok %ldsat", sats);
    return true;
  }

  // No usable fix right now (e.g. receiver lost the fix): restart the consistency count.
  void noFix(long sats) {
    _stable_count = 0;
    snprintf(_fix_status, sizeof(_fix_status), "no fix %ldsat", sats);
  }

  // GPS time is only written to the clock while a trusted fix is current, it is not
  // older than this firmware, and (once the clock is plausible) it only nudges the clock.
  bool acceptTime(uint32_t gps_time, uint32_t clock_time) {
    if (!_enabled) return true;
    const char* why = NULL;
    long step = 0;
    if (!trustedFixIsCurrent()) {
      why = "nofix";
    } else if (gps_time < minEpoch() || gps_time > maxEpoch()) {
      why = "range";
    } else if (clock_time >= minEpoch() && clock_time <= maxEpoch()) {
      // The clock already holds a plausible time. A clock outside that window (e.g. a
      // bogus 2080 date at boot) is treated like an unset one, so GPS can correct it.
      step = (long)gps_time - (long)clock_time;
      long limit = GPS_GUARD_TIME_MAX_STEP;
      if (_time_accepted) {
        // After the first accepted sync only allow corrections the size of real clock
        // drift, so a spoofer cannot walk the clock away in many small steps.
        uint32_t elapsed_s = (uint32_t)(millis() - _last_time_accept_ms) / 1000;
        limit = GPS_GUARD_TIME_DRIFT_BASE + (long)((uint64_t)elapsed_s * GPS_GUARD_TIME_DRIFT_PPM / 1000000UL);
        if (limit > GPS_GUARD_TIME_MAX_STEP) limit = GPS_GUARD_TIME_MAX_STEP;
      }
      if (labs(step) > limit) why = "step";
    }

    if (why) {
      if (step != 0) {
        snprintf(_time_status, sizeof(_time_status), "rej %s %lds", why, step);
        trackTimeDisagreement(step);
      } else {
        snprintf(_time_status, sizeof(_time_status), "rej %s", why);
      }
      return false;
    }
    snprintf(_time_status, sizeof(_time_status), "ok %+lds", (long)gps_time - (long)clock_time);
    // Every accepted sync anchors the drift budget (fail closed: no walking the clock in
    // repeated MAX_STEP jumps). A wrongly anchored time is recovered with a manual clock
    // set, which resets the anchor (clockSetManually()).
    _time_disagree = false;
    _time_alert = false;
    _time_accepted = true;
    _last_time_accept_ms = millis();
    return true;
  }

  // The clock was set by hand: drop the drift anchor and any alert.
  void clockSetManually() {
    _time_accepted = false;
    _time_disagree = false;
    _time_alert = false;
  }

  // Operator confirms that the alerted GPS time is right. offset = current GPS minus clock.
  // Returns true if the caller may write the GPS time to the clock now.
  bool acceptAlertedTime(long offset, uint32_t gps_time) {
    if (!_time_alert || !trustedFixIsCurrent()) return false;
    if (labs(offset - _time_disagree_offset) > GPS_GUARD_ALERT_TOLERANCE) return false;  // not the alerted offset
    if (gps_time < minEpoch() || gps_time > maxEpoch()) return false;
    _time_accepted = true;
    _last_time_accept_ms = millis();
    _time_disagree = false;
    _time_alert = false;
    snprintf(_time_status, sizeof(_time_status), "ok manual");
    return true;
  }

  // e.g. "ok 7sat time ok +1s", "ok 7sat time wait", "rej region time rej nofix",
  // "ok 9sat time rej step 3600s ALERT gps-clk +3600s 75m"
  void formatStatus(char* out, size_t out_size) const {
    if (!_enabled) {
      snprintf(out, out_size, "off");
      return;
    }
    // The time status is only refreshed when a sync is due. Without a current trusted fix
    // (e.g. GPS asleep between periodic fixes) an old "ok" is not shown as current: the last
    // check passed and the next one waits for a trusted fix.
    const char* time_status = (_time_status[0] == 'o' && !trustedFixIsCurrent()) ? "wait" : _time_status;
    if (_time_alert) {
      snprintf(out, out_size, "%s time %s ALERT gps-clk %+lds %lum", _fix_status, time_status,
               _time_disagree_offset, (unsigned long)((uint32_t)(millis() - _time_disagree_since_ms) / 60000));
    } else {
      snprintf(out, out_size, "%s time %s", _fix_status, time_status);
    }
  }
};
