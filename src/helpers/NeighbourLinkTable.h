#pragma once

#include <Mesh.h>        // MAX_HASH_SIZE
#include <string.h>

// --- Inter-neighbour reach graph for coverage-test flood suppression --------
//
// Records DIRECTED "can hear" edges among the NEAR neighbours of this repeater.
// An edge src->dst means "dst can hear src's transmissions" (src REACHES dst).
// RF links are frequently ASYMMETRIC (A hears B but not vice versa), so direction
// matters: inferring "N heard fi" from an observation that only "fi heard N"
// would mark N falsely covered -> M would suppress and starve N (the deafening
// this feature exists to prevent). Edges are therefore directed and never flipped.
//
// Direction is established by ACTIVE TRACE measurement (simple_repeater): the
// repeater sends a coverage TRACE [a,b,self] that returns to it; the SNR measured
// at b of a's forward tells whether b can hear a -> a reaches b -> directed edge
// a->b is recorded. (Earlier revisions inferred this passively from consecutive
// flood-path hops; that built up too slowly in sparse/mast topologies, so the
// graph is now measured.)
//
// simple_repeater uses these edges to INFER coverage: if a near neighbour fi
// forwarded flood F, then every near neighbour N with a fresh edge fi->N very
// likely also received F (N heard fi's forward). Coverage is 1-hop, NOT
// transitive.
//
// Edges are keyed by PATH HASH (the public-key prefix), NOT by neighbour-table
// index, so they survive LRU reordering of MyMesh::neighbours[].  A stored
// hash_size records the width at which the edge was measured; LOOKUPS (hasEdge)
// are width-tolerant and match on the COMMON PREFIX (min of the query and stored
// widths), so a 2-byte measured edge is found by a query of ANY width -- it is
// never missed solely because the caller used a different hash width.  Small ring
// with TTL eviction (~36 h -- repeater topology is stable); swept from loop().
//
// --- Negative-result cache (per-pair exponential backoff) -------------------
//
// A directed pair that was actively TRACE-probed but produced NO edge -- because
// the trace timed out (after its single retry) or returned below snr_lo -- is
// recorded in a separate ring (see NegLink) so stepCoverageMeasurement() does NOT
// re-probe it every cadence tick.  The re-probe backoff is PER PAIR and EXPONENTIAL:
// the first failure waits BASE (~2 min) -- so a transient cause (a momentarily-silent
// forwarder, a brief collision) heals on the next probe -- and each CONSECUTIVE failure
// doubles the wait, capped at MAX (~10 h).  A permanently-absent pair therefore ramps
// 2,4,8,... min up to one re-probe per ~10 h (the same steady state as a flat 10 h TTL,
// but without the 10 h blind spot for transients), while a good link that recovers is
// cleared immediately by addEdge() (a positive edge supersedes the record).  This cache
// is consulted ONLY to gate re-probing; coverage inference reads POSITIVE edges
// exclusively (absence is never treated as coverage).

#ifndef NEIGHBOUR_LINK_TABLE_SIZE
  #define NEIGHBOUR_LINK_TABLE_SIZE   128
#endif

#ifndef NEIGHBOUR_LINK_TTL_MILLIS
  #define NEIGHBOUR_LINK_TTL_MILLIS   (36UL * 60UL * 60UL * 1000UL)   // ~36h -- coverage is re-measured on expiry
#endif

#ifndef NEIGHBOUR_LINK_NEG_HASH_SIZE
  #define NEIGHBOUR_LINK_NEG_HASH_SIZE  2    // TRACE coverage hashes are 2 bytes; negatives are stored exact-width
#endif
#ifndef NEIGHBOUR_LINK_NEG_TABLE_SIZE
  #define NEIGHBOUR_LINK_NEG_TABLE_SIZE  32  // ~20 directed pairs among 5 near neighbours + churn headroom
#endif
#ifndef NEIGHBOUR_LINK_NEG_BACKOFF_BASE_MILLIS
  #define NEIGHBOUR_LINK_NEG_BACKOFF_BASE_MILLIS  (2UL * 60UL * 1000UL)            // first backoff after a fresh "no edge" probe (~2 min); a transient cause (a momentarily-silent forwarder, a brief collision) heals on the next probe
#endif
#ifndef NEIGHBOUR_LINK_NEG_BACKOFF_MAX_MILLIS
  #define NEIGHBOUR_LINK_NEG_BACKOFF_MAX_MILLIS   (10UL * 60UL * 60UL * 1000UL)    // cap: each consecutive failure doubles the wait up to ~10 h, so a permanently-absent pair settles to one re-probe per ~10 h (same steady state as a flat 10 h TTL) while a transient one recovers in minutes
#endif

class NeighbourLinkTable {
  struct Link {
    uint8_t  src[MAX_HASH_SIZE];   // reacher (the earlier hop on the recording path)
    uint8_t  dst[MAX_HASH_SIZE];   // reached  (the later hop -- it heard src)
    uint8_t  hash_size;
    uint32_t last_seen_ms;
    bool     active;
  };

  Link _links[NEIGHBOUR_LINK_TABLE_SIZE];
  int  _next_idx;

  // Compact negative-result ring. Fixed-width hashes (measurement is always at
  // NEIGHBOUR_LINK_NEG_HASH_SIZE), so -- unlike Link -- no variable width is stored.
  struct NegLink {
    uint8_t  src[NEIGHBOUR_LINK_NEG_HASH_SIZE];
    uint8_t  dst[NEIGHBOUR_LINK_NEG_HASH_SIZE];
    uint32_t last_seen_ms;
    uint32_t backoff_ms;   // per-pair re-probe backoff; doubles on each consecutive failure, capped at NEIGHBOUR_LINK_NEG_BACKOFF_MAX_MILLIS
    bool     active;
  };
  NegLink _neg[NEIGHBOUR_LINK_NEG_TABLE_SIZE];
  int     _neg_next_idx;

  static bool _same(const uint8_t* x, const uint8_t* y, uint8_t hs) {
    return memcmp(x, y, hs) == 0;
  }

public:
  NeighbourLinkTable() { clear(); }

  void clear() {
    memset(_links, 0, sizeof(_links));
    memset(_neg, 0, sizeof(_neg));
    _next_idx = 0;
    _neg_next_idx = 0;
  }

  // Record/refresh a DIRECTED edge src->dst (hs-byte path hashes). src reaches dst.
  // A bidirectional link occupies two separate entries (src->dst and dst->src),
  // each observed and refreshed independently -- this preserves asymmetry. Dedup
  // here is EXACT-width (only an identical-width entry is refreshed): unlike the
  // prefix-tolerant hasEdge() lookup, the WRITE side must NOT merge two distinct
  // neighbours that merely share a short common prefix. (simple_repeater records
  // only at TRACE_MEAS_HASH_SIZE, so distinct measurements are never coalesced.)
  void addEdge(const uint8_t* src, const uint8_t* dst, uint8_t hs, uint32_t now) {
    _clearNegative(src, dst, hs);                     // a positive edge supersedes a stale "no edge" record
    for (int i = 0; i < NEIGHBOUR_LINK_TABLE_SIZE; i++) {
      Link& l = _links[i];
      if (l.active && l.hash_size == hs && _same(l.src, src, hs) && _same(l.dst, dst, hs)) {
        l.last_seen_ms = now;          // refresh existing directed edge
        return;
      }
    }
    Link& l = _links[_next_idx];                 // LRU ring overwrite
    _next_idx = (_next_idx + 1) % NEIGHBOUR_LINK_TABLE_SIZE;
    memcpy(l.src, src, hs);                      // only hs bytes are meaningful
    memcpy(l.dst, dst, hs);
    l.hash_size = hs;
    l.last_seen_ms = now;
    l.active = true;
  }

  // Is there a FRESH directed edge src->dst? (i.e. dst can hear src). WIDTH-TOLERANT
  // PREFIX match: the edge may have been recorded at a hash width that differs from
  // this query's `hs`, so we compare the COMMON PREFIX -- min(hs, l.hash_size) bytes
  // -- instead of requiring an exact width. Edges are measured at TRACE_MEAS_HASH_SIZE
  // (2 bytes); thus a WIDER query (hs>2) matches on the 2 measured bytes (no loss
  // beyond measurement resolution), and a NARROWER query (hs=1) matches on 1 byte (a
  // small collision approximation -- two neighbours sharing that byte cannot be told
  // apart at that width). simple_repeater always queries at the measurement width (2),
  // so this is primarily a robustness safety net: a measured edge is never missed
  // solely because a caller happened to use a different hash width.
  bool hasEdge(const uint8_t* src, const uint8_t* dst, uint8_t hs, uint32_t now) const {
    for (int i = 0; i < NEIGHBOUR_LINK_TABLE_SIZE; i++) {
      const Link& l = _links[i];
      if (!l.active || _expired(l, now)) continue;
      uint8_t m = (hs < l.hash_size) ? hs : l.hash_size;   // common-prefix width
      if (_same(l.src, src, m) && _same(l.dst, dst, m)) {
        return true;
      }
    }
    return false;
  }

  // Evict expired edges. Call from loop().
  void purge(uint32_t now) {
    for (int i = 0; i < NEIGHBOUR_LINK_TABLE_SIZE; i++) {
      if (_links[i].active && _expired(_links[i], now)) {
        _links[i].active = false;
      }
    }
  }

  // --- Negative-result cache (probed but no edge) --------------------------
  // Record/refresh a directed "probed, no edge" result src->dst. `hs` is expected
  // to equal NEIGHBOUR_LINK_NEG_HASH_SIZE (kept for API symmetry with addEdge).
  // Mirrors addEdge: dedup + refresh, else LRU ring insert. Called from MyMesh on
  // TRACE 2nd-miss timeout and on weak return (SNR < snr_lo).
  void addNegative(const uint8_t* src, const uint8_t* dst, uint8_t hs, uint32_t now) {
    (void)hs;   // fixed-width NEIGHBOUR_LINK_NEG_HASH_SIZE; callers always pass that
    for (int i = 0; i < NEIGHBOUR_LINK_NEG_TABLE_SIZE; i++) {
      NegLink& n = _neg[i];
      if (n.active && _same(n.src, src, NEIGHBOUR_LINK_NEG_HASH_SIZE) && _same(n.dst, dst, NEIGHBOUR_LINK_NEG_HASH_SIZE)) {
        // Re-probed and failed AGAIN -> looks permanent: double the backoff (capped at
        // MAX), so a persistently-absent pair is retried ever more rarely. A transient
        // failure never reaches this branch twice -- it is cleared by addEdge() on success.
        if (n.backoff_ms == 0) n.backoff_ms = NEIGHBOUR_LINK_NEG_BACKOFF_BASE_MILLIS;
        n.backoff_ms *= 2;
        if (n.backoff_ms > NEIGHBOUR_LINK_NEG_BACKOFF_MAX_MILLIS) n.backoff_ms = NEIGHBOUR_LINK_NEG_BACKOFF_MAX_MILLIS;
        n.last_seen_ms = now;          // restart the (now longer) backoff window
        return;
      }
    }
    NegLink& n = _neg[_neg_next_idx];              // LRU ring overwrite
    _neg_next_idx = (_neg_next_idx + 1) % NEIGHBOUR_LINK_NEG_TABLE_SIZE;
    memcpy(n.src, src, NEIGHBOUR_LINK_NEG_HASH_SIZE);
    memcpy(n.dst, dst, NEIGHBOUR_LINK_NEG_HASH_SIZE);
    n.backoff_ms = NEIGHBOUR_LINK_NEG_BACKOFF_BASE_MILLIS;   // first failure -> short backoff (fast retry)
    n.last_seen_ms = now;
    n.active = true;
  }

  // Fresh "probed, no edge" record for src->dst? (`hs` expected == NEIGHBOUR_LINK_NEG_HASH_SIZE.)
  // Consulted ONLY by stepCoverageMeasurement to skip re-probing; NEVER affects coverage inference.
  bool hasNegative(const uint8_t* src, const uint8_t* dst, uint8_t hs, uint32_t now) const {
    (void)hs;
    for (int i = 0; i < NEIGHBOUR_LINK_NEG_TABLE_SIZE; i++) {
      const NegLink& n = _neg[i];
      if (!n.active || _neg_expired(n, now)) continue;
      if (_same(n.src, src, NEIGHBOUR_LINK_NEG_HASH_SIZE) && _same(n.dst, dst, NEIGHBOUR_LINK_NEG_HASH_SIZE)) return true;
    }
    return false;
  }

  // Reclaim ring slots for pairs long unre-probed. The threshold is well beyond the max
  // backoff (2 x MAX), so a pair that is capped at MAX -- which re-probes and refreshes
  // itself every MAX -- is NOT evicted mid-ramp (that would reset it to BASE and re-probe
  // it far too often). The LRU ring would reclaim the slot anyway; this just defers churn.
  void purgeNegative(uint32_t now) {
    for (int i = 0; i < NEIGHBOUR_LINK_NEG_TABLE_SIZE; i++) {
      if (_neg[i].active && (uint32_t)(now - _neg[i].last_seen_ms) > (2UL * NEIGHBOUR_LINK_NEG_BACKOFF_MAX_MILLIS)) _neg[i].active = false;
    }
  }

private:
  static bool _expired(const Link& l, uint32_t now) {
    // uint32 subtraction is wrap-safe for any ttl well below the wrap period.
    return (uint32_t)(now - l.last_seen_ms) > NEIGHBOUR_LINK_TTL_MILLIS;
  }

  // Has this pair's per-pair re-probe backoff elapsed? (i.e. it is eligible to be
  // probed again -- hasNegative returns false for it). uint32 subtraction is wrap-safe.
  static bool _neg_expired(const NegLink& n, uint32_t now) {
    return (uint32_t)(now - n.last_seen_ms) > n.backoff_ms;
  }

  // A fresh positive edge src->dst supersedes any stale "no edge" record for the same
  // directed pair (a link that has improved). Prefix match on the common width --
  // addEdge's hs is always the measurement width (2) == NEIGHBOUR_LINK_NEG_HASH_SIZE,
  // but stay tolerant if hs ever differs.
  void _clearNegative(const uint8_t* src, const uint8_t* dst, uint8_t hs) {
    uint8_t m = (hs < (uint8_t)NEIGHBOUR_LINK_NEG_HASH_SIZE) ? hs : (uint8_t)NEIGHBOUR_LINK_NEG_HASH_SIZE;
    for (int i = 0; i < NEIGHBOUR_LINK_NEG_TABLE_SIZE; i++) {
      NegLink& n = _neg[i];
      if (n.active && _same(n.src, src, m) && _same(n.dst, dst, m)) n.active = false;
    }
  }
};
