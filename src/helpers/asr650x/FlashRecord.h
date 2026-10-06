#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// 160-byte on-flash record (EEPROM emulation, SFLASH). Explicit little-endian layout:
//   0..3 "KTM1" | 4 version(1) | 5 flags(bit0 identity, bit1 prefs) | 6..7 0
//   8..71 prv | 72..103 pub | 104..135 name | 136..139 freq | 140..143 bw
//   144 sf | 145 cr | 146 tx_power | 147 gps_off | 148..151 seq | 152..155 gps_interval | 156..159 CRC-32 of bytes 0..155
namespace asr650x {

const size_t PERSIST_SIZE = 160;

struct PersistState {
  uint32_t seq;               // record generation, newest valid row wins (see FlashRecordStore.h)
  bool has_identity;
  uint8_t prv[64];
  uint8_t pub[32];
  bool has_prefs;
  char node_name[32];
  float freq, bw;
  uint8_t sf, cr;
  int8_t tx_power_dbm;
  bool gps_off;               // 1 = the user switched GPS off (0 = default: on, also for older records)
  uint32_t gps_interval;      // seconds between GPS refreshes, 0 = only at boot / on request
};

inline uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

inline void persist_encode(const PersistState& s, uint8_t out[PERSIST_SIZE]) {
  memset(out, 0, PERSIST_SIZE);
  memcpy(out, "KTM1", 4);
  out[4] = 1;
  out[5] = (s.has_identity ? 1 : 0) | (s.has_prefs ? 2 : 0);
  if (s.has_identity) { memcpy(out + 8, s.prv, 64); memcpy(out + 72, s.pub, 32); }
  if (s.has_prefs) {
    memcpy(out + 104, s.node_name, 32);
    memcpy(out + 136, &s.freq, 4);
    memcpy(out + 140, &s.bw, 4);
    out[144] = s.sf; out[145] = s.cr; out[146] = (uint8_t)s.tx_power_dbm;
  }
  out[147] = s.gps_off ? 1 : 0;
  for (int i = 0; i < 4; i++) out[148 + i] = (uint8_t)(s.seq >> (8 * i));
  for (int i = 0; i < 4; i++) out[152 + i] = (uint8_t)(s.gps_interval >> (8 * i));
  uint32_t crc = crc32(out, 156);
  for (int i = 0; i < 4; i++) out[156 + i] = (uint8_t)(crc >> (8 * i));
}

inline bool persist_decode(const uint8_t in[PERSIST_SIZE], PersistState& s) {
  memset(&s, 0, sizeof(s));
  if (memcmp(in, "KTM1", 4) != 0 || in[4] != 1) return false;
  uint32_t want = 0;
  for (int i = 0; i < 4; i++) want |= (uint32_t)in[156 + i] << (8 * i);
  if (crc32(in, 156) != want) return false;
  s.gps_off = in[147] != 0;
  for (int i = 0; i < 4; i++) s.seq |= (uint32_t)in[148 + i] << (8 * i);
  for (int i = 0; i < 4; i++) s.gps_interval |= (uint32_t)in[152 + i] << (8 * i);
  s.has_identity = (in[5] & 1) != 0;
  s.has_prefs = (in[5] & 2) != 0;
  if (s.has_identity) { memcpy(s.prv, in + 8, 64); memcpy(s.pub, in + 72, 32); }
  if (s.has_prefs) {
    memcpy(s.node_name, in + 104, 32); s.node_name[31] = 0;
    memcpy(&s.freq, in + 136, 4);
    memcpy(&s.bw, in + 140, 4);
    s.sf = in[144]; s.cr = in[145]; s.tx_power_dbm = (int8_t)in[146];
  }
  return true;
}

}  // namespace asr650x
