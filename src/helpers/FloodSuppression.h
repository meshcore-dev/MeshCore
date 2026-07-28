#pragma once

#include <Mesh.h>        // MAX_HASH_SIZE
#include <string.h>

// --- Coverage-test FLOOD suppression ---------------------------------------
//
// Per-flood (packet-hash) bookkeeping used by simple_repeater to suppress
// redundant re-broadcasts.  The packet hash (Packet::calculatePacketHash) is
// path-independent for FLOOD packets, so the original, every overheard forward
// and our own scheduled outbound re-broadcast all share ONE hash identity.
//
// M suppresses its rebroadcast of flood F if every NEAR neighbour is already
// known to have F.  "Known to have F" = the neighbour appears on the path of
// some forward of F that M overheard -- every hop on a decoded path forwarded F,
// so it has it (certain evidence, no SNR/inference).
// This is sound for directional/co-located antennas -- a downstream neighbour
// that has not received F is on no path M decodes, so it stays uncovered and M
// forwards.
//
// Per entry we keep a small dedup set of the near-neighbour indices seen on
// overheard forwards' paths (indices into MyMesh::neighbours[]).  Suppression
// fires when that set spans all CURRENT near neighbours (checked from MyMesh,
// which owns the neighbour table and the "near" definition: fresh + SNR>=snr_lo).
//
// The table is a small ring with TTL eviction (swept from loop()).  It is app
// local and touches neither the core dedup table nor the persisted prefs.

#ifndef FLOOD_SUPPRESS_TABLE_SIZE
  #define FLOOD_SUPPRESS_TABLE_SIZE   32
#endif

#ifndef FLOOD_SUPPRESS_TTL_MILLIS
  #define FLOOD_SUPPRESS_TTL_MILLIS   10000
#endif

// Max near neighbours recordable as "covered" per flood.  Saturating is safe:
// a flood with more near neighbours than this can never confirm coverage, so M
// forwards (conservative).  16 covers typical dense clusters.
#ifndef FLOOD_SUPPRESS_COVERAGE_SET_SIZE
  #define FLOOD_SUPPRESS_COVERAGE_SET_SIZE  16
#endif

struct FloodSuppressionEntry {
  uint8_t  hash[MAX_HASH_SIZE];
  uint8_t  covered[FLOOD_SUPPRESS_COVERAGE_SET_SIZE]; // near-neighbour indices known to have this flood
  uint8_t  covered_count;
  uint32_t first_seen_ms;           // for TTL eviction
  bool     suppressed;              // our rebroadcast already cancelled/suppressed
  bool     active;

  // Record a near-neighbour index as covered (dedup). Returns true if newly added.
  bool addCovered(uint8_t idx) {
    for (uint8_t i = 0; i < covered_count; i++)
      if (covered[i] == idx) return false;
    if (covered_count < FLOOD_SUPPRESS_COVERAGE_SET_SIZE) {
      covered[covered_count++] = idx;
      return true;
    }
    return false;   // set full -> can't confirm coverage for this idx (safe: M forwards)
  }

  // Is the given near-neighbour index already known covered?
  bool covers(uint8_t idx) const {
    for (uint8_t i = 0; i < covered_count; i++)
      if (covered[i] == idx) return true;
    return false;
  }
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
    e->covered_count = 0;
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
