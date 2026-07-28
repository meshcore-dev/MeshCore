#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock SHA256 class for testing.
// Uses void* params to match the real Crypto library signatures, so it can back
// both Utils.cpp and TransportKeyStore.cpp in native builds (the digest output is
// not meaningful under the mock; tests must not depend on actual hash values).
class SHA256 {
public:
  void update(const void* data, size_t len) { (void)data; (void)len; }
  void finalize(void* hash, size_t hashLen) { (void)hash; (void)hashLen; }
  void resetHMAC(const void* key, size_t keyLen) { (void)key; (void)keyLen; }
  void finalizeHMAC(const void* key, size_t keyLen, void* hash, size_t hashLen) {
    (void)key; (void)keyLen; (void)hash; (void)hashLen;
  }
};
