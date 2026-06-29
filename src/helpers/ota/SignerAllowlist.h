#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Runtime-managed allowlist of trusted Ed25519 firmware-signer public keys (docs/ota_protocol.md §9,
// decision D2 + Q1: no key embedded in firmware; only allowlist-signed firmware may auto-apply).
// Portable + fixed-capacity (no dynamic allocation). Persistence (load/save) is layered on per-platform.

namespace mesh {
namespace ota {

#ifndef MAX_OTA_SIGNERS
#define MAX_OTA_SIGNERS 4
#endif

class SignerAllowlist {
  uint8_t _keys[MAX_OTA_SIGNERS][32];
  uint8_t _count = 0;

public:
  void clear() { _count = 0; }
  uint8_t count() const { return _count; }
  const uint8_t* get(uint8_t i) const { return (i < _count) ? _keys[i] : nullptr; }

  bool contains(const uint8_t* pub) const {
    for (uint8_t i = 0; i < _count; i++)
      if (memcmp(_keys[i], pub, 32) == 0) return true;
    return false;
  }

  // Add a key (idempotent). Returns false if the list is full.
  bool add(const uint8_t* pub) {
    if (contains(pub)) return true;
    if (_count >= MAX_OTA_SIGNERS) return false;
    memcpy(_keys[_count++], pub, 32);
    return true;
  }

  bool remove(const uint8_t* pub) {
    for (uint8_t i = 0; i < _count; i++) {
      if (memcmp(_keys[i], pub, 32) == 0) {
        memmove(_keys[i], _keys[i + 1], (size_t)(_count - i - 1) * 32);
        _count--;
        return true;
      }
    }
    return false;
  }

  // Serialize as: count(1) || key0(32) || key1(32) ...  Returns bytes written.
  uint32_t serialize(uint8_t* out, uint32_t max_len) const {
    uint32_t need = 1 + (uint32_t)_count * 32;
    if (max_len < need) return 0;
    out[0] = _count;
    memcpy(out + 1, _keys, (size_t)_count * 32);
    return need;
  }

  bool deserialize(const uint8_t* in, uint32_t len) {
    if (len < 1) return false;
    uint8_t n = in[0];
    if (n > MAX_OTA_SIGNERS || (uint32_t)1 + n * 32 > len) return false;
    _count = n;
    memcpy(_keys, in + 1, (size_t)n * 32);
    return true;
  }
};

} // namespace ota
} // namespace mesh
