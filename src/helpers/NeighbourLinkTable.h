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
// Direction comes for free from the flood path: path hops are in forwarding order
// [R1..Rn], so a consecutive pair (X, Y) means Y forwarded right after X, i.e.
// Y heard X -> X reaches Y -> directed edge X->Y is recorded.
//
// simple_repeater uses these edges to INFER coverage: if a near neighbour fi
// forwarded flood F, then every near neighbour N with a fresh edge fi->N very
// likely also received F (N heard fi's forward). Coverage is 1-hop, NOT
// transitive.
//
// Edges are keyed by PATH HASH (the public-key prefix), NOT by neighbour-table
// index, so they survive LRU reordering of MyMesh::neighbours[].  A stored
// hash_size lets edges coexist across deployments of different hash widths
// (VER_1 -> 1 byte); only that many bytes are ever compared.  Small ring with
// TTL eviction (~1 day -- repeater topology is stable); swept from loop().

#ifndef NEIGHBOUR_LINK_TABLE_SIZE
  #define NEIGHBOUR_LINK_TABLE_SIZE   128
#endif

#ifndef NEIGHBOUR_LINK_TTL_MILLIS
  #define NEIGHBOUR_LINK_TTL_MILLIS   (24UL * 60UL * 60UL * 1000UL)   // ~1 day
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

  static bool _same(const uint8_t* x, const uint8_t* y, uint8_t hs) {
    return memcmp(x, y, hs) == 0;
  }

public:
  NeighbourLinkTable() { clear(); }

  void clear() {
    memset(_links, 0, sizeof(_links));
    _next_idx = 0;
  }

  // Record/refresh a DIRECTED edge src->dst (hs-byte path hashes). src reaches dst.
  // A bidirectional link occupies two separate entries (src->dst and dst->src),
  // each observed and refreshed independently -- this preserves asymmetry.
  void addEdge(const uint8_t* src, const uint8_t* dst, uint8_t hs, uint32_t now) {
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

  // Is there a FRESH directed edge src->dst (hs-byte hashes)? (i.e. dst can hear src)
  bool hasEdge(const uint8_t* src, const uint8_t* dst, uint8_t hs, uint32_t now) const {
    for (int i = 0; i < NEIGHBOUR_LINK_TABLE_SIZE; i++) {
      const Link& l = _links[i];
      if (l.active && l.hash_size == hs && !_expired(l, now) &&
          _same(l.src, src, hs) && _same(l.dst, dst, hs)) {
        return true;
      }
    }
    return false;
  }

  // Evict expired edges. Call from loop().
  void purge(uint32_t now) {
    for (int i = 0; i < NEIGHBOUR_LINK_TABLE_SIZE; i++) {
      if (_links[i].active && _expired(_links[i], now)) _links[i].active = false;
    }
  }

private:
  static bool _expired(const Link& l, uint32_t now) {
    // uint32 subtraction is wrap-safe for any ttl well below the wrap period.
    return (uint32_t)(now - l.last_seen_ms) > NEIGHBOUR_LINK_TTL_MILLIS;
  }
};
