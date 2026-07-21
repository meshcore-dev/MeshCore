#pragma once

#if defined(OTA_SD_SEEDER)

#include "OtaStore.h"

namespace mesh {
namespace ota {

// OtaStore that captures an in-transit `.mota` onto SD as OTA_SD_DIR/<midhex>.mota.part -> .mota.
class SdMotaStore : public OtaStore {
public:
  SdMotaStore() = default;

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
  bool plan_layout(bool, uint32_t, uint32_t, uint32_t) override { return true; }

private:
  bool openPart(bool create, uint32_t fill_size = 0);
  void partPath(char* out, size_t cap) const;

  uint8_t  _mid[4] = {0};
  uint32_t _total = 0;
  mutable void* _file = nullptr;   // FsFile* while open (lazy)
};

} // namespace ota
} // namespace mesh

#endif
