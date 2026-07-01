#pragma once

#include <stdint.h>
#include <stddef.h>

// A tiny bounds-checked little-endian cursor for reading the `.mota` container (docs/ota_protocol.md §3-§4)
// in a self-documenting way: each field is read by name in order, instead of hand-computed byte offsets
// (`p[0]`, `rd_u32(p+3)`, `p += 89`, `NEED(n)` ...). Any over-read flips `ok` false and yields zero/null, so
// callers parse the whole struct then check `r.ok` once. 32-bit offsets (a container can be >64 KB; the
// 16-byte LoRa wire messages keep their own uint16 cursor in OtaProtocol.cpp). No allocation; `take()`
// returns a pointer INTO the caller's buffer (zero-copy), matching the manifest's by-pointer fields.

namespace mesh {
namespace ota {

// Little-endian scalar read/write for RANDOM-access fields (a specific offset into a buffer, e.g. a wire
// row or a fixed manifest slot). Sequential parsing should prefer the ByteReader cursor below. These
// replace the per-file `rd_u32`/`wr_u32` helpers that were copy-pasted across the OTA sources.
inline uint16_t rd_u16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
inline uint32_t rd_u32le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void wr_u16le(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void wr_u32le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Round to a multiple of `unit` (a power of two — a flash sector/page size). Names the `& ~(unit-1)`
// idiom so flash-geometry math in the stores reads as intent (align down / align up).
inline uint32_t align_down(uint32_t x, uint32_t unit) { return x & ~(unit - 1); }
inline uint32_t align_up(uint32_t x, uint32_t unit)   { return (x + unit - 1) & ~(unit - 1); }

struct ByteReader {
  const uint8_t* p;
  uint32_t len;
  uint32_t n = 0;
  bool ok = true;

  ByteReader(const uint8_t* buf, uint32_t length) : p(buf), len(length) {}

  uint32_t pos() const { return n; }
  bool fits(uint32_t k) const { return ok && (uint64_t)n + k <= len; }

  uint8_t u8() { if (!fits(1)) { ok = false; return 0; } return p[n++]; }
  uint32_t u32() {                                   // little-endian
    if (!fits(4)) { ok = false; return 0; }
    uint32_t v = rd_u32le(p + n); n += 4; return v;
  }
  // Borrow `k` bytes at the cursor (e.g. merkle_root[4], leaves[4*BC]) and advance; null on overflow.
  const uint8_t* take(uint32_t k) {
    if (!fits(k)) { ok = false; return nullptr; }
    const uint8_t* r = p + n; n += k; return r;
  }
  void skip(uint32_t k) { if (!fits(k)) { ok = false; return; } n += k; }
};

} // namespace ota
} // namespace mesh
