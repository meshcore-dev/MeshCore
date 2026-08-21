#pragma once

#if defined(OTA_SEEDER_CACHE)

#include "OtaStore.h"
#include <string.h>

namespace mesh {
namespace ota {

// OtaStore that captures an in-transit `.mota` onto the seeder FS as
// OTA_SEEDER_DIR/<midhex>.mota.part -> .mota.
class SeederMotaStore : public OtaStore {
public:
  SeederMotaStore() = default;

  void set_mid(const uint8_t mid[4]) { memcpy(_mid, mid, 4); }

  bool begin(uint32_t total_size) override;
  bool write(uint32_t off, const uint8_t* data, uint32_t len) override;
  bool read(uint32_t off, uint8_t* buf, uint32_t len) const override;
  uint32_t capacity() const override { return 0xF0000000u; }
  uint32_t staged_size() const override { return _total; }
  void clear() override;
  void finalize() override;
  bool reopen() override;

  bool set_meta_size(uint32_t) override { return true; }
  // Superseeder library is deltas-only — refuse full snapshots at store admission.
  bool plan_layout(bool is_full, uint32_t, uint32_t, uint32_t) override { return !is_full; }

private:
  void partPath(char* out, size_t cap) const;

  uint8_t  _mid[4] = {0};
  uint32_t _total = 0;
};

}  // namespace ota
}  // namespace mesh

#endif
