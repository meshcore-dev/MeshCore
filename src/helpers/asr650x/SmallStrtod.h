#pragma once

#include <stdint.h>
#include <stddef.h>

// Decimal-only strtod() (optional sign, digits, optional fraction, optional exponent; no hex/inf/nan).
// newlib's strtod() pulls in ~7 KB of flash (gdtoa); the CLI only parses plain decimal numbers.
// SmallStrtod.cpp installs it as strtod/strtof/atof on ASR650x builds.
inline double asr650x_strtod(const char* s, char** end) {
  const char* p = s;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') p++;
  bool neg = false;
  if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
  uint64_t mant = 0;
  int digits = 0, scale = 0;
  bool any = false;
  while (*p >= '0' && *p <= '9') {
    any = true;
    if (digits < 19) { mant = mant * 10 + (uint64_t)(*p - '0'); digits += (mant != 0); }
    else scale++;                                  // more digits than fit: keep the magnitude only
    p++;
  }
  if (*p == '.') {
    const char* q = p + 1;
    bool frac = false;
    while (*q >= '0' && *q <= '9') {
      frac = true;
      if (digits < 19) { mant = mant * 10 + (uint64_t)(*q - '0'); digits += (mant != 0); scale--; }
      q++;
    }
    if (any || frac) { any = true; p = q; }
  }
  if (!any) {
    if (end) *end = (char*)s;
    return 0.0;
  }
  if (*p == 'e' || *p == 'E') {
    const char* q = p + 1;
    bool eneg = false;
    if (*q == '+' || *q == '-') { eneg = (*q == '-'); q++; }
    if (*q >= '0' && *q <= '9') {
      int e = 0;
      while (*q >= '0' && *q <= '9') { if (e < 400) e = e * 10 + (*q - '0'); q++; }
      scale += eneg ? -e : e;
      p = q;
    }
  }
  double v = (double)mant;
  while (scale > 0) { v *= 10.0; scale--; }
  while (scale < 0) { v /= 10.0; scale++; }
  if (end) *end = (char*)p;
  return neg ? -v : v;
}
