#pragma once

#include "FlashRecord.h" // crc32

#include <stdint.h>
#include <string.h>

// Pinned contacts: up to 3 contacts that the app added or edited survive a reboot (the rest of the contact
// table stays in RAM). Stored in ONE SFLASH user row: the core lets user code write rows 0..2, and rows 0/1
// hold the A/B identity + prefs record, so there is no A/B copy here. A power loss while this row is written
// loses the pins (CRC fails -> empty set), never the identity. Layout (210 bytes, little-endian): 0..3 "PIN1"
// | 4 count | 5..7 0 | 8 + i*66: pub[32] name[32] type flags
//                                    | 206..209 CRC-32 of bytes 0..205
namespace asr650x {

const int PIN_MAX = 3;
const size_t PIN_ENTRY = 66;
const size_t PIN_SIZE = 8 + PIN_MAX * PIN_ENTRY + 4;

struct PinEntry {
  uint8_t pub[32];
  char name[32]; // NUL-terminated, at most 31 characters
  uint8_t type;
  uint8_t flags;
};

struct PinSet {
  uint8_t n;
  PinEntry e[PIN_MAX];
  PinSet() : n(0) { memset(e, 0, sizeof(e)); }
};

enum PinResult { PIN_SAME = 0, PIN_CHANGED = 1, PIN_FULL = 2 };

inline int pinFind(const PinSet &s, const uint8_t pub[32]) {
  for (int i = 0; i < s.n; i++)
    if (memcmp(s.e[i].pub, pub, 32) == 0) return i;
  return -1;
}

inline PinResult pinSetFields(PinEntry &e, const char *name, uint8_t type, uint8_t flags) {
  char nm[32];
  memset(nm, 0, sizeof(nm));
  if (name) strncpy(nm, name, 31);
  if (memcmp(e.name, nm, 32) == 0 && e.type == type && e.flags == flags) return PIN_SAME;
  memcpy(e.name, nm, 32);
  e.type = type;
  e.flags = flags;
  return PIN_CHANGED;
}

// pins (or refreshes) a contact; PIN_FULL when 3 others are pinned already
inline PinResult pinUpsert(PinSet &s, const uint8_t pub[32], const char *name, uint8_t type, uint8_t flags) {
  int i = pinFind(s, pub);
  if (i >= 0) return pinSetFields(s.e[i], name, type, flags);
  if (s.n >= PIN_MAX) return PIN_FULL;
  PinEntry &e = s.e[s.n++];
  memset(&e, 0, sizeof(e));
  memcpy(e.pub, pub, 32);
  pinSetFields(e, name, type, flags);
  return PIN_CHANGED;
}

// updates the stored copy of an already pinned contact; never pins a new one
inline PinResult pinRefresh(PinSet &s, const uint8_t pub[32], const char *name, uint8_t type, uint8_t flags) {
  int i = pinFind(s, pub);
  return i < 0 ? PIN_SAME : pinSetFields(s.e[i], name, type, flags);
}

inline bool pinRemove(PinSet &s, const uint8_t pub[32]) {
  int i = pinFind(s, pub);
  if (i < 0) return false;
  for (int k = i; k + 1 < s.n; k++)
    s.e[k] = s.e[k + 1];
  s.n--;
  memset(&s.e[s.n], 0, sizeof(s.e[s.n]));
  return true;
}

inline void pinsEncode(const PinSet &s, uint8_t out[PIN_SIZE]) {
  memset(out, 0, PIN_SIZE);
  memcpy(out, "PIN1", 4);
  out[4] = s.n;
  for (int i = 0; i < s.n && i < PIN_MAX; i++) {
    uint8_t *p = out + 8 + i * PIN_ENTRY;
    memcpy(p, s.e[i].pub, 32);
    memcpy(p + 32, s.e[i].name, 31); // byte 63 stays 0: always terminated
    p[64] = s.e[i].type;
    p[65] = s.e[i].flags;
  }
  uint32_t crc = crc32(out, PIN_SIZE - 4);
  for (int k = 0; k < 4; k++)
    out[PIN_SIZE - 4 + k] = (uint8_t)(crc >> (8 * k));
}

// false (and an empty set) for an erased, corrupt or foreign row
inline bool pinsDecode(const uint8_t in[PIN_SIZE], PinSet &s) {
  s = PinSet();
  if (memcmp(in, "PIN1", 4) != 0) return false;
  uint32_t want = 0;
  for (int k = 0; k < 4; k++)
    want |= (uint32_t)in[PIN_SIZE - 4 + k] << (8 * k);
  if (crc32(in, PIN_SIZE - 4) != want) return false;
  if (in[4] > PIN_MAX) return false;
  s.n = in[4];
  for (int i = 0; i < s.n; i++) {
    const uint8_t *p = in + 8 + i * PIN_ENTRY;
    memcpy(s.e[i].pub, p, 32);
    memcpy(s.e[i].name, p + 32, 31);
    s.e[i].name[31] = 0;
    s.e[i].type = p[64];
    s.e[i].flags = p[65];
  }
  return true;
}

} // namespace asr650x
