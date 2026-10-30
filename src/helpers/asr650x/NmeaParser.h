#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Integer-only NMEA 0183 reader for the Air530 (GGA + RMC). No float/double, fixed buffer, tolerant of noise.
namespace asr650x {

enum NmeaEvent { NMEA_NONE = 0, NMEA_GGA = 1, NMEA_RMC = 2, NMEA_BAD = 3 };

struct GpsData {
  bool pos_valid;         // latest GGA/RMC carried a usable position
  uint8_t fix_quality;    // GGA field 6 (0 = none)
  uint8_t sats;           // GGA field 7
  int32_t lat_e6, lon_e6; // degrees x 1e6
  int32_t alt_dm;         // decimetres
  bool rmc_valid;         // RMC status 'A'
  uint32_t time_hhmmss;   // UTC, from RMC
  uint32_t date_ddmmyy;   // from RMC
};

inline uint8_t nmeaChecksum(const char *s, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++)
    c ^= (uint8_t)s[i];
  return c;
}

// "$body*HH\r\n"; returns the length, or 0 if out is too small.
inline int nmeaMake(char *out, size_t out_size, const char *body) {
  size_t n = strlen(body);
  if (out_size < n + 7) return 0; // $ + body + * + 2 hex + \r\n + NUL
  static const char H[] = "0123456789ABCDEF";
  uint8_t c = nmeaChecksum(body, n);
  out[0] = '$';
  memcpy(out + 1, body, n);
  out[1 + n] = '*';
  out[2 + n] = H[c >> 4];
  out[3 + n] = H[c & 15];
  out[4 + n] = '\r';
  out[5 + n] = '\n';
  out[6 + n] = 0;
  return (int)(n + 6);
}

inline bool nmeaIsLeap(int y) {
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

// UTC epoch from RMC date/time. 0 means invalid (year outside 2024..2060, bad month/day/hour/minute/second).
inline uint32_t gpsToEpoch(uint32_t date_ddmmyy, uint32_t time_hhmmss) {
  int dd = (int)(date_ddmmyy / 10000), mm = (int)((date_ddmmyy / 100) % 100), yy = (int)(date_ddmmyy % 100);
  int hh = (int)(time_hhmmss / 10000), mi = (int)((time_hhmmss / 100) % 100), ss = (int)(time_hhmmss % 100);
  int year = 2000 + yy;
  if (year < 2024 || year > 2060) return 0;
  if (mm < 1 || mm > 12 || dd < 1) return 0;
  static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int days_in_month = dim[mm - 1] + ((mm == 2 && nmeaIsLeap(year)) ? 1 : 0);
  if (dd > days_in_month) return 0;
  if (hh > 23 || mi > 59 || ss > 59) return 0;
  uint32_t days = 0;
  for (int y = 1970; y < year; y++)
    days += nmeaIsLeap(y) ? 366 : 365;
  for (int m = 1; m < mm; m++)
    days += dim[m - 1] + ((m == 2 && nmeaIsLeap(year)) ? 1 : 0);
  days += (uint32_t)(dd - 1);
  return days * 86400u + (uint32_t)hh * 3600u + (uint32_t)mi * 60u + (uint32_t)ss;
}

class NmeaParser {
  char _buf[84];
  uint8_t _len;
  bool _active;
  GpsData _d;
  uint32_t _ok, _bad;

  static bool parseUint(const char *s, uint32_t *out) { // digits only, non-empty
    if (!*s) return false;
    uint32_t v = 0;
    for (; *s; s++) {
      if (*s < '0' || *s > '9') return false;
      v = v * 10 + (uint32_t)(*s - '0');
      if (v > 99999999u) return false;
    }
    *out = v;
    return true;
  }

  // "ddmm.mmmm" (or dddmm.mmmm) + hemisphere -> degrees x 1e6; false if malformed or empty
  static bool parseLatlon(const char *s, char hemi, int deg_digits, int32_t *out) {
    if (!*s) return false;
    int n = 0;
    while (s[n] >= '0' && s[n] <= '9')
      n++;
    if (n != deg_digits + 2) return false;
    uint32_t deg = 0;
    for (int i = 0; i < deg_digits; i++)
      deg = deg * 10 + (uint32_t)(s[i] - '0');
    uint32_t whole = (uint32_t)(s[deg_digits] - '0') * 10 + (uint32_t)(s[deg_digits + 1] - '0');
    const char *p = s + n;
    uint64_t frac = 0, scale = 1; // fractional minutes, up to 6 digits
    if (*p == '.') {
      p++;
      int k = 0;
      while (*p >= '0' && *p <= '9') {
        if (k < 6) {
          frac = frac * 10 + (uint64_t)(*p - '0');
          scale *= 10;
          k++;
        }
        p++;
      }
    }
    if (*p != 0) return false;
    if (whole >= 60) return false;
    uint64_t minutes_scaled = (uint64_t)whole * scale + frac;                     // minutes x scale
    uint64_t frac_e6 = (minutes_scaled * 1000000u + 30u * scale) / (60u * scale); // rounded
    int64_t v = (int64_t)deg * 1000000 + (int64_t)frac_e6;
    if (v > (deg_digits == 2 ? 90000000LL : 180000000LL)) return false; // beyond the pole / the antimeridian
    if (hemi == 'S' || hemi == 'W')
      v = -v;
    else if (hemi != 'N' && hemi != 'E')
      return false;
    *out = (int32_t)v;
    return true;
  }

  // "123.4" -> decimetres (one decimal, truncated); false if malformed
  static bool parseDm(const char *s, int32_t *out) {
    if (!*s) return false;
    bool neg = false;
    if (*s == '-') {
      neg = true;
      s++;
    }
    if (*s < '0' || *s > '9') return false;
    int32_t v = 0;
    while (*s >= '0' && *s <= '9') {
      v = v * 10 + (*s - '0');
      s++;
      if (v > 1000000) return false;
    }
    int32_t d = 0;
    if (*s == '.') {
      s++;
      if (*s >= '0' && *s <= '9') d = *s - '0';
      while (*s >= '0' && *s <= '9')
        s++;
    }
    if (*s != 0) return false;
    v = v * 10 + d;
    *out = neg ? -v : v;
    return true;
  }

  static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }

  NmeaEvent parse() {
    _buf[_len] = 0;
    char *star = (char *)memchr(_buf, '*', _len);
    if (!star || (_buf + _len) - star != 3) return NMEA_BAD;
    int h1 = hex(star[1]), h2 = hex(star[2]);
    if (h1 < 0 || h2 < 0) return NMEA_BAD;
    if (nmeaChecksum(_buf, (size_t)(star - _buf)) != (uint8_t)(h1 * 16 + h2)) return NMEA_BAD;
    *star = 0;

    char *f[20];
    int nf = 0;
    f[nf++] = _buf;
    for (char *p = _buf; *p && nf < 20; p++)
      if (*p == ',') {
        *p = 0;
        f[nf++] = p + 1;
      }
    size_t idlen = strlen(f[0]);
    if (idlen < 5) {
      _ok++;
      return NMEA_NONE;
    }
    const char *type = f[0] + idlen - 3;

    if (strcmp(type, "GGA") == 0 && nf >= 10) {
      uint32_t q = 0, sats = 0;
      parseUint(f[6], &q);
      parseUint(f[7], &sats);
      _d.fix_quality = (uint8_t)(q > 255 ? 255 : q);
      _d.sats = (uint8_t)(sats > 255 ? 255 : sats);
      int32_t lat = 0, lon = 0, alt = 0;
      bool ok =
          _d.fix_quality > 0 && parseLatlon(f[2], f[3][0], 2, &lat) && parseLatlon(f[4], f[5][0], 3, &lon);
      if (ok) {
        _d.lat_e6 = lat;
        _d.lon_e6 = lon;
        if (parseDm(f[9], &alt)) _d.alt_dm = alt;
      }
      _d.pos_valid = ok;
      _ok++;
      return NMEA_GGA;
    }
    if (strcmp(type, "RMC") == 0 && nf >= 10) {
      bool a = f[2][0] == 'A' && f[2][1] == 0;
      _d.rmc_valid = false;
      uint32_t t = 0, dt = 0;
      int32_t lat = 0, lon = 0;
      bool ok = a && parseLatlon(f[3], f[4][0], 2, &lat) && parseLatlon(f[5], f[6][0], 3, &lon);
      // time "hhmmss[.sss]": keep the integer part
      char tb[8];
      size_t tl = 0;
      while (f[1][tl] && f[1][tl] != '.' && tl < 7) {
        tb[tl] = f[1][tl];
        tl++;
      }
      tb[tl] = 0;
      bool tok = tl == 6 && parseUint(tb, &t);
      bool dok = strlen(f[9]) == 6 && parseUint(f[9], &dt);
      if (a && tok && dok) {
        _d.rmc_valid = true;
        _d.time_hhmmss = t;
        _d.date_ddmmyy = dt;
      }
      if (ok) {
        _d.lat_e6 = lat;
        _d.lon_e6 = lon;
      }
      _d.pos_valid = ok;
      _ok++;
      return NMEA_RMC;
    }
    _ok++;
    return NMEA_NONE;
  }

public:
  NmeaParser() : _len(0), _active(false), _ok(0), _bad(0) { memset(&_d, 0, sizeof(_d)); }

  const GpsData &data() const { return _d; }
  void resetFix() {
    _d.pos_valid = false;
    _d.rmc_valid = false;
    _d.fix_quality = 0;
    _d.sats = 0;
  } // after a power cycle: old data is not a fix
  uint32_t sentencesOk() const { return _ok; }
  uint32_t sentencesBad() const { return _bad; }

  NmeaEvent feed(uint8_t c) {
    if (c == '$') { // (re)start; a '$' inside a broken sentence is a resync
      if (_active && _len > 0) _bad++;
      _len = 0;
      _active = true;
      return NMEA_NONE;
    }
    if (!_active) return NMEA_NONE;
    if (c == '\r' || c == '\n') {
      if (_len == 0) return NMEA_NONE;
      NmeaEvent e = parse();
      if (e == NMEA_BAD) _bad++;
      _active = false;
      _len = 0;
      return e;
    }
    if (_len >= sizeof(_buf) - 1) { // overlong: drop it
      _active = false;
      _len = 0;
      _bad++;
      return NMEA_BAD;
    }
    _buf[_len++] = (char)c;
    return NMEA_NONE;
  }
};

} // namespace asr650x
