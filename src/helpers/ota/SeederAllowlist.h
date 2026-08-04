#pragma once

#if defined(OTA_SUPERSEEDER)

#include "OtaFormat.h"
#include <stdint.h>
#include <string.h>

namespace mesh {
namespace ota {

#ifndef MAX_OTA_SEEDER_ALLOW
#define MAX_OTA_SEEDER_ALLOW 8
#endif

// Runtime target allowlist for the superseeder library.
//   allow-all (default): _filter == false
//   filter mode:         _filter == true — only listed ids; empty list admits nothing
// Deltas-only is always enforced separately via ota_seeder_is_delta().
class SeederAllowlist {
  uint32_t _ids[MAX_OTA_SEEDER_ALLOW] = {};
  uint8_t  _count = 0;
  bool     _filter = false;  // false = admit all targets

public:
  // Empty the filter list and stay in filter mode (admit nothing until add/reset).
  void clear() { _count = 0; _filter = true; }
  // Restore factory default: admit all targets.
  void reset() { _count = 0; _filter = false; }

  uint8_t count() const { return _count; }
  bool filtering() const { return _filter; }
  bool allowAll() const { return !_filter; }
  uint32_t get(uint8_t i) const { return (i < _count) ? _ids[i] : 0; }

  bool contains(uint32_t target_id) const {
    for (uint8_t i = 0; i < _count; i++)
      if (_ids[i] == target_id) return true;
    return false;
  }

  bool admits(uint32_t target_id) const {
    if (!_filter) return true;
    return contains(target_id);
  }

  bool add(uint32_t target_id) {
    if (target_id == 0) return false;
    _filter = true;
    if (contains(target_id)) return true;
    if (_count >= MAX_OTA_SEEDER_ALLOW) return false;
    _ids[_count++] = target_id;
    return true;
  }

  bool remove(uint32_t target_id) {
    for (uint8_t i = 0; i < _count; i++) {
      if (_ids[i] == target_id) {
        memmove(&_ids[i], &_ids[i + 1], (size_t)(_count - i - 1) * sizeof(uint32_t));
        _count--;
        _filter = true;  // stay filtered even if now empty
        return true;
      }
    }
    return false;
  }
};

inline bool ota_seeder_is_delta(uint8_t codec, uint8_t flags) {
  if ((flags & MFLAG_FULL) != 0) return false;
  if (codec == CODEC_FULL) return false;
  return codec == CODEC_DETOOLS_INPLACE || codec == CODEC_DETOOLS_SEQUENTIAL;
}

inline bool ota_seeder_admit(const SeederAllowlist& allow, uint32_t target_id,
                             uint8_t codec, uint8_t flags) {
  return allow.admits(target_id) && ota_seeder_is_delta(codec, flags);
}

}  // namespace ota
}  // namespace mesh

#endif
