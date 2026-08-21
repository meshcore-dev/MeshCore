#pragma once

#if defined(OTA_SEEDER_STORAGE)

#include "OtaSource.h"
#include "SeederAllowlist.h"

namespace mesh {
namespace ota {

// MotaSource over complete delta `.mota` files on the seeder FS (OTA_SEEDER_DIR).
class MotaSourceSeeder : public MotaSource {
public:
  static const uint8_t MAX_FILES = 64;

  void setAllowlist(const SeederAllowlist* allow) { _allow = allow; }
  void refresh();

  uint8_t count() override { return _count; }
  uint8_t cachedCount() const { return _count; }
  uint32_t cachedTotalBytes() const { return _total_bytes; }
  bool describe(uint8_t idx, MotaDesc& out) override;
  bool read(uint8_t idx, uint32_t off, uint8_t* buf, uint32_t len) override;

  bool hasMid(const uint8_t mid[4]) const;

private:
  const SeederAllowlist* _allow = nullptr;
  char     _paths[MAX_FILES][40] = {};
  uint8_t  _count = 0;
  uint32_t _total_bytes = 0;

  bool describePath(const char* path, MotaDesc& out) const;
};

}  // namespace ota
}  // namespace mesh

#endif
