#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PUB_KEY_SIZE
#define PUB_KEY_SIZE            32
#endif

#ifndef MAX_TRUSTED_CLOCK_KEYS
#define MAX_TRUSTED_CLOCK_KEYS  4
#endif

#define TRUSTED_CLOCK_NAME_LEN  16
#define MAX_SYNC_EVENTS         2

struct TrustedClockSource {
  uint8_t  pub_key[PUB_KEY_SIZE];
  uint32_t last_timestamp;                   // RAM-only replay cursor; resets on reboot
  char     name[TRUSTED_CLOCK_NAME_LEN];     // "" if unset
};

struct ClockSyncEvent {
  uint8_t  pub_key_prefix[8];
  char     name[TRUSTED_CLOCK_NAME_LEN];
  uint32_t local_before;
  uint32_t advert_timestamp;
};

class TrustedClockTable {
public:
  static const uint8_t FORMAT_VERSION = 0x02;
  static const size_t  RECORD_SIZE    = PUB_KEY_SIZE + TRUSTED_CLOCK_NAME_LEN;
  static const size_t  HEADER_SIZE    = 2;   // version + count

  TrustedClockTable() { reset(); }

  void reset() {
    _count = 0;
    _num_events = 0;
    _next_event_idx = 0;
    memset(_sources, 0, sizeof(_sources));
    memset(_events, 0, sizeof(_events));
  }

  uint8_t count() const                              { return _count; }
  bool    isFull() const                             { return _count >= MAX_TRUSTED_CLOCK_KEYS; }
  const TrustedClockSource& at(uint8_t i) const      { return _sources[i]; }
  TrustedClockSource&       at(uint8_t i)            { return _sources[i]; }

  TrustedClockSource* find(const uint8_t* pub_key);
  TrustedClockSource* findByName(const char* name);
  // Returns true if added or already present (name updated when non-NULL); false if full and key not present.
  bool add(const uint8_t* pub_key, const char* name);
  bool remove(const uint8_t* pub_key);

  // Pure predicates (also exposed for unit tests)
  static bool     passesReplay(uint32_t advert_ts, uint32_t last_ts) { return advert_ts > last_ts; }
  static uint32_t absDiff(uint32_t a, uint32_t b)                    { return a > b ? a - b : b - a; }
  static bool     shouldStep(uint32_t now, uint32_t advert_ts, uint16_t threshold) {
    if (threshold == 0) return false;
    return absDiff(now, advert_ts) >= threshold;
  }

  // Sync-event ring (newest-first iteration helper)
  uint8_t syncEventCount() const { return _num_events; }
  const ClockSyncEvent& syncEventNewestFirst(uint8_t idx_from_newest) const;
  void recordSyncEvent(const uint8_t* pub_key, const char* name,
                       uint32_t local_before, uint32_t advert_timestamp);

  // Pure serialization for the on-disk format. No filesystem dependency.
  // Format: u8 version, u8 count, then count × {pub_key[32], name[16]}.
  // serialize() returns bytes written; deserialize() returns true on a valid header.
  // last_timestamp is intentionally NOT persisted (RAM-only).
  size_t serializedSize() const { return HEADER_SIZE + (size_t)_count * RECORD_SIZE; }
  size_t serialize(uint8_t* buf, size_t cap) const;
  bool   deserialize(const uint8_t* buf, size_t len);

private:
  TrustedClockSource _sources[MAX_TRUSTED_CLOCK_KEYS];
  uint8_t            _count;
  ClockSyncEvent     _events[MAX_SYNC_EVENTS];
  uint8_t            _num_events;
  uint8_t            _next_event_idx;
};
