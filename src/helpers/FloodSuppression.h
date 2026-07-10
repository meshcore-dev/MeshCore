#pragma once

#include <Mesh.h>        // MAX_HASH_SIZE
#include <string.h>

// --- Redundancy-aware FLOOD suppression -----------------------------------
//
// Per-flood (packet-hash) bookkeeping used by simple_repeater to suppress
// redundant re-broadcasts.  The packet hash (Packet::calculatePacketHash) is
// path-independent for FLOOD packets, so the original, every overheard forward
// and our own scheduled outbound re-broadcast all share ONE hash identity.
//
// On each received flood copy (counted in MyMesh::logRx, i.e. at RX-arrival
// time, BEFORE calcRxDelay) we accumulate a weighted overheard-copy count.
// When it reaches the threshold C the entry is flagged `suppressed` and the
// already-scheduled outbound re-broadcast is cancelled (redundant).
//
// Weighting gives the SNR "distance" bias its correct sign:
//   - a STRONG overheard forward (you are central / redundant) counts more,
//   - a WEAK   overheard forward (you are at the edge, extending reach) is
//     ignored (weight 0) so reach is preserved.
//
// The table is a small ring with TTL eviction (swept from loop()).  It is app
// local and touches neither the core dedup table nor the persisted prefs.

#ifndef FLOOD_SUPPRESS_TABLE_SIZE
  #define FLOOD_SUPPRESS_TABLE_SIZE   32
#endif

#ifndef FLOOD_SUPPRESS_TTL_MILLIS
  #define FLOOD_SUPPRESS_TTL_MILLIS   10000
#endif

struct FloodSuppressionEntry {
  uint8_t  hash[MAX_HASH_SIZE];
  uint8_t  weighted_count;          // weighted number of overheard forwards
  int8_t   first_snr_x4;            // SNR of the first copy seen (x4, signed)
  int8_t   strongest_overheard_x4;  // strongest overheard forward SNR (x4)
  uint32_t first_seen_ms;           // for TTL eviction
  bool     suppressed;              // our rebroadcast already cancelled/suppressed
  bool     active;
};

class FloodSuppressionTable {
  FloodSuppressionEntry _entries[FLOOD_SUPPRESS_TABLE_SIZE];
  int _next_idx;

public:
  FloodSuppressionTable() { clear(); }

  void clear() {
    memset(_entries, 0, sizeof(_entries));
    _next_idx = 0;
  }

  // Lookup a live (active, non-expired) entry. Returns NULL if none.
  FloodSuppressionEntry* find(const uint8_t* hash, uint32_t now) {
    for (int i = 0; i < FLOOD_SUPPRESS_TABLE_SIZE; i++) {
      FloodSuppressionEntry& e = _entries[i];
      if (e.active && !_expired(e, now) && memcmp(hash, e.hash, MAX_HASH_SIZE) == 0) {
        return &e;
      }
    }
    return NULL;
  }

  // Find or create an entry. *is_new is set true when a fresh entry was created.
  FloodSuppressionEntry* touch(const uint8_t* hash, uint32_t now, bool* is_new) {
    FloodSuppressionEntry* e = find(hash, now);
    if (e) { if (is_new) *is_new = false; return e; }

    e = &_entries[_next_idx];                       // LRU ring overwrite
    _next_idx = (_next_idx + 1) % FLOOD_SUPPRESS_TABLE_SIZE;
    memcpy(e->hash, hash, MAX_HASH_SIZE);
    e->weighted_count = 0;
    e->first_snr_x4 = 0;
    e->strongest_overheard_x4 = -128;               // sentinel: "none seen"
    e->first_seen_ms = now;
    e->suppressed = false;
    e->active = true;
    if (is_new) *is_new = true;
    return e;
  }

  // Evict expired entries. Call from loop().
  void purge(uint32_t now) {
    for (int i = 0; i < FLOOD_SUPPRESS_TABLE_SIZE; i++) {
      if (_entries[i].active && _expired(_entries[i], now)) {
        _entries[i].active = false;
      }
    }
  }

private:
  static bool _expired(const FloodSuppressionEntry& e, uint32_t now) {
    // uint32 subtraction is wrap-safe for any ttl well below the wrap period.
    return (uint32_t)(now - e.first_seen_ms) > FLOOD_SUPPRESS_TTL_MILLIS;
  }
};
