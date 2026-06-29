#pragma once

#include <stdint.h>
#include <stddef.h>
#include "Utils.h"        // mesh::Utils::sha256 (real on device; real host SHA-256 via test/mocks)
#include "OtaFormat.h"

// Thin multihash helpers: SHA-256 truncated to N bytes. No state, no allocation.

namespace mesh {
namespace ota {

inline void sha256_trunc(uint8_t* out, size_t out_len, const uint8_t* data, size_t len) {
  mesh::Utils::sha256(out, out_len, data, (int)len);
}

inline void sha256_trunc2(uint8_t* out, size_t out_len,
                          const uint8_t* a, size_t a_len,
                          const uint8_t* b, size_t b_len) {
  mesh::Utils::sha256(out, out_len, a, (int)a_len, b, (int)b_len);
}

inline void mh4(uint8_t out[4], const uint8_t* data, size_t len)  { sha256_trunc(out, 4, data, len); }
inline void mh8(uint8_t out[8], const uint8_t* data, size_t len)  { sha256_trunc(out, 8, data, len); }
inline void mh32(uint8_t out[32], const uint8_t* data, size_t len){ sha256_trunc(out, 32, data, len); }

} // namespace ota
} // namespace mesh
