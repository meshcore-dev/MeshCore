#pragma once

#ifdef ENABLE_COMPRESSION

#include <stdint.h>

namespace mesh {

// Returns compressed length, or -1 if compression would make it larger.
// Output buffer must be at least `in_len` bytes.
int compressPayload(const uint8_t* in, int in_len, uint8_t* out, int out_max);

// Returns decompressed length, or -1 on error.
int decompressPayload(const uint8_t* in, int in_len, uint8_t* out, int out_max);

} // namespace mesh

#endif // ENABLE_COMPRESSION
