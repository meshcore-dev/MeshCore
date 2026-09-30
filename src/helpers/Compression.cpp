#ifdef ENABLE_COMPRESSION

#include "Compression.h"
#include "unishox2.h"

#include <string.h>

namespace mesh {

int compressPayload(const uint8_t* in, int in_len, uint8_t* out, int out_max) {
    int compressed_len = unishox2_compress_simple(
        reinterpret_cast<const char*>(in),
        in_len,
        reinterpret_cast<char*>(out)
    );

    if (compressed_len < 0 || compressed_len >= in_len) {
        return -1;
    }

    return compressed_len;
}

int decompressPayload(const uint8_t* in, int in_len, uint8_t* out, int out_max) {
    int decompressed_len = unishox2_decompress_simple(
        reinterpret_cast<const char*>(in),
        in_len,
        reinterpret_cast<char*>(out)
    );

    if (decompressed_len < 0) {
        return -1;
    }

    return decompressed_len;
}

} // namespace mesh

#endif // ENABLE_COMPRESSION
