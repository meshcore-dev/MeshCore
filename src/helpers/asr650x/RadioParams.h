#pragma once
#include <stdint.h>
#include <math.h>

// Pure helpers for the SX1262 wrapper (host-testable). Bandwidth table order is the Heltec driver's
// `Bandwidths[]` (libraries/LoraWan102/src/radio/radio.c): 125, 250, 500, 62.5, 41.67, 31.25, 20.83, 15.63, 10.42, 7.81 kHz.
namespace asr650x {

inline float bw_value(int index) {
  static const float table[10] = {125.0f, 250.0f, 500.0f, 62.5f, 41.67f, 31.25f, 20.83f, 15.63f, 10.42f, 7.81f};
  if (index < 0) index = 0;
  if (index > 9) index = 9;
  return table[index];
}

// Nearest supported bandwidth by ratio (so 100 kHz maps to 125, not to 62.5). Never returns an invalid index.
inline int bw_index(float bw_khz) {
  if (!(bw_khz > 0.0f)) return 9;                         // zero, negative or NaN -> smallest
  int best = 0;
  float best_ratio = 1e9f;
  for (int i = 0; i < 10; i++) {
    float v = bw_value(i);
    float r = bw_khz > v ? bw_khz / v : v / bw_khz;       // >= 1, closer to 1 is nearer
    if (r < best_ratio) { best_ratio = r; best = i; }
  }
  return best;
}

inline uint8_t cr_index(uint8_t cr) {                     // MeshCore 5..8 (4/5..4/8) -> Heltec 1..4
  if (cr < 5) cr = 5;
  if (cr > 8) cr = 8;
  return (uint8_t)(cr - 4);
}

inline uint8_t sf_clamp(uint8_t sf) { return sf < 5 ? 5 : (sf > 12 ? 12 : sf); }

inline uint16_t preamble_for_sf(uint8_t sf) { return sf <= 8 ? 32 : 16; }   // same rule as MeshCore's RadioLibWrapper

// Semtech time-on-air, explicit header, CRC on, integer microsecond arithmetic. Returns milliseconds (rounded up).
inline uint32_t airtime_ms(int len, float bw_khz, uint8_t sf, uint8_t cr, uint16_t preamble) {
  if (len < 0) len = 0;
  sf = sf_clamp(sf);
  uint8_t crv = cr_index(cr);                             // 1..4
  uint32_t bw_hz = (uint32_t)(bw_khz * 1000.0f + 0.5f);
  if (bw_hz == 0) bw_hz = 1;
  uint32_t tsym_us = (uint32_t)(((uint64_t)1000000u << sf) / bw_hz);
  int de = tsym_us >= 16380u ? 1 : 0;                     // low data-rate optimisation: same threshold as the Heltec driver (16.38 ms)
  int num = 8 * len - 4 * (int)sf + 28 + 16;              // +16 CRC, explicit header (no -20)
  int den = 4 * ((int)sf - 2 * de);
  int blocks = num > 0 ? (num + den - 1) / den : 0;
  uint32_t npay = 8u + (uint32_t)(blocks * (crv + 4));
  uint64_t us = ((uint64_t)(preamble * 4 + 17) * tsym_us) / 4 + (uint64_t)npay * tsym_us;   // (n_pre + 4.25) symbols
  return (uint32_t)((us + 999u) / 1000u);
}

// Same estimate MeshCore uses to weigh received packets (RadioLibWrapper::packetScoreInt).
inline float packet_score(float snr, int sf, int packet_len) {
  static const float snr_threshold[] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};   // SF7..SF12
  if (sf < 7 || sf > 12) return 0.0f;
  if (snr < snr_threshold[sf - 7]) return 0.0f;
  float success = (snr - snr_threshold[sf - 7]) / 10.0f;
  float collision_penalty = 1.0f - (packet_len / 256.0f);
  float v = success * collision_penalty;
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

}  // namespace asr650x
