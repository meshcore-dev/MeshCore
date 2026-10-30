#pragma once

#include <stdint.h>
#include <string.h>

// Single-precision powers without libm, for calcRxDelay() on boards built with RX_DELAY_FAST_POW, where
// libm's pow() and the double-precision routines it pulls in do not fit in flash. Relative error is well
// under 1 %.

// 2^y: integer power of two times a 4th-order polynomial for the fractional part
inline float fastPow2f(float y) {
  if (y > 30.0f) y = 30.0f;
  if (y < -30.0f) y = -30.0f;
  int k = (int)y;
  if ((float)k > y) k--;  // floor
  float f = y - (float)k; // [0, 1)
  float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
  return k >= 0 ? p * (float)(1u << k) : p / (float)(1u << (-k));
}

// log2(x) for x > 0: exponent from the IEEE-754 bits, mantissa m in [1, 2) through ln(m) = 2
// atanh((m-1)/(m+1))
inline float fastLog2f(float x) {
  uint32_t bits;
  memcpy(&bits, &x, 4);
  int e = (int)((bits >> 23) & 0xFF) - 127;
  bits = (bits & 0x007FFFFFu) | 0x3F800000u; // mantissa as a float in [1, 2)
  float m;
  memcpy(&m, &bits, 4);
  float z = (m - 1.0f) / (m + 1.0f); // [0, 1/3)
  float z2 = z * z;
  float ln_m = 2.0f * z * (1.0f + z2 * (0.3333333f + z2 * (0.2f + z2 * 0.1428571f)));
  return (float)e + ln_m * 1.4426950f; // 1 / ln(2)
}

// base^x for base > 0
inline float fastPowf(float base, float x) {
  return fastPow2f(x * fastLog2f(base));
}

// 10^x
inline float fastPow10f(float x) {
  return fastPow2f(x * 3.3219281f);
}
