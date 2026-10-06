#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// NOTE: no %l length modifier anywhere: the core's wrapped snprintf printed "u" for "%lu" on the device.
namespace asr650x {

struct StatusInput {
  const char* name;
  uint8_t id4[4];            // first bytes of the public key
  bool time_valid;
  uint32_t epoch;            // UTC seconds
  uint8_t gps_state;         // 0 off, 1 searching, 2 fixed, 3 timeout
  uint8_t sats;
  bool pos_valid;
  int32_t lat_e6, lon_e6;
  uint16_t batt_mv;          // 0 = not measured
  uint8_t contacts;          // contacts in the table
  uint8_t max_contacts;      // table size (MAX_CONTACTS)
  uint32_t freq_khz;
  uint8_t sf;
  uint32_t warming_s;
};

// The text of screen line `index` (0..7), at most 21 characters (plus the NUL in out[22]).
inline void status_line(int index, const StatusInput& in, char out[22]) {
  char t[48];
  t[0] = 0;
  switch (index) {
    case 0: {
      size_t n = 0;
      if (in.name) {
        for (; in.name[n] && n < 21; n++) {
          unsigned char c = (unsigned char)in.name[n];
          t[n] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
        }
      }
      t[n] = 0;
      break;
    }
    case 1: snprintf(t, sizeof(t), "ID %02x%02x%02x%02x", (unsigned)in.id4[0], (unsigned)in.id4[1], (unsigned)in.id4[2], (unsigned)in.id4[3]); break;
    case 2:
      if (!in.time_valid) snprintf(t, sizeof(t), "--:--:-- no time");
      else {
        uint32_t s = in.epoch % 86400u;
        snprintf(t, sizeof(t), "%02u:%02u:%02u UTC", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
      }
      break;
    case 3:
      if (in.gps_state == 1) snprintf(t, sizeof(t), "GPS search %us", (unsigned)(in.warming_s > 999 ? 999 : in.warming_s));
      else if (in.gps_state == 2) snprintf(t, sizeof(t), "GPS fix %u sats", (unsigned)in.sats);
      else if (in.gps_state == 3) snprintf(t, sizeof(t), "GPS timeout");
      else snprintf(t, sizeof(t), "GPS off");
      break;
    case 4:
    case 5: {
      if (!in.pos_valid) { snprintf(t, sizeof(t), index == 4 ? "LAT --" : "LON --"); break; }
      int32_t v = index == 4 ? in.lat_e6 : in.lon_e6;
      bool neg = v < 0;
      uint32_t a = neg ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
      // sign and whole degrees are right-aligned in a fixed field so the decimals line up
      char whole[12];
      snprintf(whole, sizeof(whole), "%s%u", neg ? "-" : "", (unsigned)(a / 1000000u));
      snprintf(t, sizeof(t), "%s %3s.%06u", index == 4 ? "LAT" : "LON", whole, (unsigned)(a % 1000000u));
      break;
    }
    case 6: {
      char b[10];
      if (in.batt_mv == 0) snprintf(b, sizeof(b), "--");
      else snprintf(b, sizeof(b), "%u.%02uV", (unsigned)(in.batt_mv / 1000), (unsigned)((in.batt_mv % 1000) / 10));
      snprintf(t, sizeof(t), "BAT %s  CT %u/%u", b, (unsigned)in.contacts, (unsigned)in.max_contacts);
      break;
    }
    case 7:
      snprintf(t, sizeof(t), "%u.%03uMHz SF%u", (unsigned)(in.freq_khz / 1000), (unsigned)(in.freq_khz % 1000), (unsigned)in.sf);
      break;
    default: break;
  }
  strncpy(out, t, 21);
  out[21] = 0;
}

}  // namespace asr650x
