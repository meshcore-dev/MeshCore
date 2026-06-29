#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Block-availability helpers (docs/ota_protocol.md §7).
//
// Availability is *derived* from the staged manifest's leaves[]: block i is present iff its 4-byte
// leaf slot is non-erased (!= FF FF FF FF). No separate persistent structure. A compact bitmap
// (1 bit/block) is used on the wire (OTA_HAVE) and as an in-RAM cache. All ops are caller-buffer
// based; no allocation.

namespace mesh {
namespace ota {

inline bool leaf_present(const uint8_t* leaves, uint32_t i) {
  const uint8_t* p = leaves + (size_t)i * 4;
  return !(p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF && p[3] == 0xFF);
}

inline uint32_t bitmap_bytes(uint32_t block_count) { return (block_count + 7) / 8; }

inline bool bitmap_get(const uint8_t* bm, uint32_t i) {
  return (bm[i >> 3] >> (i & 7)) & 1;
}

inline void bitmap_set(uint8_t* bm, uint32_t i, bool v) {
  uint8_t mask = (uint8_t)(1u << (i & 7));
  if (v) bm[i >> 3] |= mask; else bm[i >> 3] &= (uint8_t)~mask;
}

// Build a bitmap (caller buffer >= bitmap_bytes(count)) from leaves[].
inline void leaves_to_bitmap(const uint8_t* leaves, uint32_t count, uint8_t* bm_out) {
  memset(bm_out, 0, bitmap_bytes(count));
  for (uint32_t i = 0; i < count; i++)
    if (leaf_present(leaves, i)) bitmap_set(bm_out, i, true);
}

inline uint32_t count_present(const uint8_t* leaves, uint32_t count) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < count; i++) if (leaf_present(leaves, i)) n++;
  return n;
}

inline bool all_present(const uint8_t* leaves, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) if (!leaf_present(leaves, i)) return false;
  return true;
}

} // namespace ota
} // namespace mesh
