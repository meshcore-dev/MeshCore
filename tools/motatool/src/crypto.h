// SHA-256 (multihash truncations) and Ed25519 — backed by OpenSSL libcrypto, so the tool is portable
// across Ubuntu / Raspberry Pi / any arch that has OpenSSL (and Android NDK for a future BLE seeder).
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <vector>

namespace mota {

// SHA-256 of `data`, truncated to out_len (sha2-256:N). out_len <= 32.
void sha256_trunc(uint8_t* out, size_t out_len, const uint8_t* data, size_t len);

inline std::array<uint8_t,4>  mh4(const uint8_t* d, size_t n)  { std::array<uint8_t,4>  o; sha256_trunc(o.data(),4,d,n);  return o; }
inline std::array<uint8_t,8>  mh8(const uint8_t* d, size_t n)  { std::array<uint8_t,8>  o; sha256_trunc(o.data(),8,d,n);  return o; }
inline std::array<uint8_t,32> mh32(const uint8_t* d, size_t n) { std::array<uint8_t,32> o; sha256_trunc(o.data(),32,d,n); return o; }

// Ed25519. Keys are raw 32-byte (private seed / public key), matching motalib / the device.
bool ed25519_verify(const uint8_t sig[64], const uint8_t* msg, size_t msg_len, const uint8_t pub[32]);
// Sign `msg` with a 32-byte raw private seed -> 64-byte signature. Returns false on error.
bool ed25519_sign(uint8_t sig[64], const uint8_t* msg, size_t msg_len, const uint8_t priv[32]);
// Derive the 32-byte raw public key from a 32-byte raw private seed.
bool ed25519_pub_from_priv(uint8_t pub[32], const uint8_t priv[32]);
// Generate a fresh keypair (raw 32-byte each).
bool ed25519_keygen(uint8_t priv[32], uint8_t pub[32]);

} // namespace mota
