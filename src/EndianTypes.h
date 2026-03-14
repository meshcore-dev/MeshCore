#pragma once

#include <stdint.h>

/**
 * @brief Endianness-safe integer types for network/protocol communication
 * 
 * These types automatically handle byte order conversion between wire format
 * and host format, preventing endianness bugs in protocol implementations.
 */

// Big-endian 32-bit unsigned integer
struct be_uint32_t {
    uint32_t wireBytes;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    operator uint32_t() const { return __builtin_bswap32(wireBytes); }
#else
    operator uint32_t() const { return wireBytes; }
#endif
} __attribute__((packed));

// Big-endian 16-bit unsigned integer
struct be_uint16_t {
    uint16_t wireBytes;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    operator uint16_t() const { return __builtin_bswap16(wireBytes); }
#else
    operator uint16_t() const { return wireBytes; }
#endif
} __attribute__((packed));

// Little-endian 16-bit unsigned integer
struct le_uint16_t {
    uint16_t wireBytes;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    operator uint16_t() const { return wireBytes; }
#else
    operator uint16_t() const { return __builtin_bswap16(wireBytes); }
#endif
} __attribute__((packed));

// Little-endian 32-bit unsigned integer
struct le_uint32_t {
    uint32_t wireBytes;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    operator uint32_t() const { return wireBytes; }
#else
    operator uint32_t() const { return __builtin_bswap32(wireBytes); }
#endif
} __attribute__((packed));