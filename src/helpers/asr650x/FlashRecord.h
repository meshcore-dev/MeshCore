#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// On-flash record (SFLASH user row). Explicit little-endian layout:
//   0..3 "ASR1" ("KTM1" in records from early builds, still accepted) | 4 version | 5 flags(bit0 identity,
//   bit1 prefs) | 6..7 0 8..71 prv | 72..103 pub | 104..135 name | 136..139 freq | 140..143 bw 144 sf | 145
//   cr | 146 tx_power | 147 gps_off | 148..151 seq | 152..155 gps_interval
// version 1 (160 bytes): 156..159 CRC-32 of bytes 0..155
// version 2 (192 bytes): 156..159 airtime_factor | 160..163 rx_delay_base | 164..167 tx_delay_factor
//   | 168..171 direct_tx_delay_factor | 172 multi_acks | 173 manual_add_contacts | 174 telemetry modes
//   | 175 advert_loc_policy | 176 autoadd_config | 177 autoadd_max_hops | 178 path_hash_mode | 179 tz_offset
//   | 180 cad_enabled | 181 interference_threshold | 182 agc_reset_interval | 183 rx_boosted_gain | 184..187
//   0 | 188..191 CRC-32 of bytes 0..187
namespace asr650x {

const size_t PERSIST_SIZE = 192; // version 2; a version 1 record uses the first 160 bytes

struct PersistState {
  uint32_t seq; // record generation, newest valid row wins (see FlashRecordStore.h)
  bool has_identity;
  uint8_t prv[64];
  uint8_t pub[32];
  bool has_prefs;
  char node_name[32];
  float freq, bw;
  uint8_t sf, cr;
  int8_t tx_power_dbm;
  bool gps_off;          // 1 = the user switched GPS off (0 = default: on, also for older records)
  uint32_t gps_interval; // seconds between GPS refreshes, 0 = only at boot / on request
  bool has_ext;          // version 2 settings below are present (false for a version 1 record)
  float airtime_factor, rx_delay_base, tx_delay_factor, direct_tx_delay_factor;
  uint8_t multi_acks, manual_add_contacts, telemetry_modes, advert_loc_policy;
  uint8_t autoadd_config, autoadd_max_hops, path_hash_mode;
  int8_t tz_offset;
  uint8_t cad_enabled, interference_threshold, agc_reset_interval, rx_boosted_gain;
};

inline uint32_t crc32(const uint8_t *d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++)
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

inline void persistEncode(const PersistState &s, uint8_t out[PERSIST_SIZE]) {
  memset(out, 0, PERSIST_SIZE);
  memcpy(out, "ASR1", 4);
  out[4] = 2;
  out[5] = (s.has_identity ? 1 : 0) | (s.has_prefs ? 2 : 0);
  if (s.has_identity) {
    memcpy(out + 8, s.prv, 64);
    memcpy(out + 72, s.pub, 32);
  }
  if (s.has_prefs) {
    memcpy(out + 104, s.node_name, 32);
    memcpy(out + 136, &s.freq, 4);
    memcpy(out + 140, &s.bw, 4);
    out[144] = s.sf;
    out[145] = s.cr;
    out[146] = (uint8_t)s.tx_power_dbm;
  }
  out[147] = s.gps_off ? 1 : 0;
  for (int i = 0; i < 4; i++)
    out[148 + i] = (uint8_t)(s.seq >> (8 * i));
  for (int i = 0; i < 4; i++)
    out[152 + i] = (uint8_t)(s.gps_interval >> (8 * i));
  if (s.has_ext) {
    memcpy(out + 156, &s.airtime_factor, 4);
    memcpy(out + 160, &s.rx_delay_base, 4);
    memcpy(out + 164, &s.tx_delay_factor, 4);
    memcpy(out + 168, &s.direct_tx_delay_factor, 4);
    out[172] = s.multi_acks;
    out[173] = s.manual_add_contacts;
    out[174] = s.telemetry_modes;
    out[175] = s.advert_loc_policy;
    out[176] = s.autoadd_config;
    out[177] = s.autoadd_max_hops;
    out[178] = s.path_hash_mode;
    out[179] = (uint8_t)s.tz_offset;
    out[180] = s.cad_enabled;
    out[181] = s.interference_threshold;
    out[182] = s.agc_reset_interval;
    out[183] = s.rx_boosted_gain;
    out[184] = 1; // marks the settings block as present
  }
  uint32_t crc = crc32(out, 188);
  for (int i = 0; i < 4; i++)
    out[188 + i] = (uint8_t)(crc >> (8 * i));
}

inline bool persistDecode(const uint8_t in[PERSIST_SIZE], PersistState &s) {
  memset(&s, 0, sizeof(s));
  if ((memcmp(in, "ASR1", 4) != 0 && memcmp(in, "KTM1", 4) != 0) || (in[4] != 1 && in[4] != 2)) return false;
  size_t crc_at = in[4] == 1 ? 156 : 188;
  uint32_t want = 0;
  for (int i = 0; i < 4; i++)
    want |= (uint32_t)in[crc_at + i] << (8 * i);
  if (crc32(in, crc_at) != want) return false;
  if (in[4] == 2 && in[184] == 1) {
    s.has_ext = true;
    memcpy(&s.airtime_factor, in + 156, 4);
    memcpy(&s.rx_delay_base, in + 160, 4);
    memcpy(&s.tx_delay_factor, in + 164, 4);
    memcpy(&s.direct_tx_delay_factor, in + 168, 4);
    s.multi_acks = in[172];
    s.manual_add_contacts = in[173];
    s.telemetry_modes = in[174];
    s.advert_loc_policy = in[175];
    s.autoadd_config = in[176];
    s.autoadd_max_hops = in[177];
    s.path_hash_mode = in[178];
    s.tz_offset = (int8_t)in[179];
    s.cad_enabled = in[180];
    s.interference_threshold = in[181];
    s.agc_reset_interval = in[182];
    s.rx_boosted_gain = in[183];
  }
  s.gps_off = in[147] != 0;
  for (int i = 0; i < 4; i++)
    s.seq |= (uint32_t)in[148 + i] << (8 * i);
  for (int i = 0; i < 4; i++)
    s.gps_interval |= (uint32_t)in[152 + i] << (8 * i);
  s.has_identity = (in[5] & 1) != 0;
  s.has_prefs = (in[5] & 2) != 0;
  if (s.has_identity) {
    memcpy(s.prv, in + 8, 64);
    memcpy(s.pub, in + 72, 32);
  }
  if (s.has_prefs) {
    memcpy(s.node_name, in + 104, 32);
    s.node_name[31] = 0;
    memcpy(&s.freq, in + 136, 4);
    memcpy(&s.bw, in + 140, 4);
    s.sf = in[144];
    s.cr = in[145];
    s.tx_power_dbm = (int8_t)in[146];
  }
  return true;
}

} // namespace asr650x
