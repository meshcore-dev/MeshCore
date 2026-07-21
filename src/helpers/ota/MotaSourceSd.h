#pragma once

#if defined(OTA_SD_SEEDER)

#include "OtaSource.h"

namespace mesh {
namespace ota {

// MotaSource over a folder of complete `.mota` files on SD (OTA_SD_DIR).
class MotaSourceSd : public MotaSource {
public:
  static const uint8_t MAX_FILES = 64;

  void refresh();

  uint8_t count() override { return _count; }
  uint8_t cachedCount() const { return _count; }
  uint32_t cachedTotalBytes() const { return _total_bytes; }
  bool    describe(uint8_t idx, MotaDesc& out) override;
  bool    read(uint8_t idx, uint32_t off, uint8_t* buf, uint32_t len) override;

  bool hasMid(const uint8_t mid[4]) const;

private:
  char     _paths[MAX_FILES][40] = {};
  uint8_t  _count = 0;
  uint32_t _total_bytes = 0;

  bool describePath(const char* path, MotaDesc& out) const;
};

} // namespace ota
} // namespace mesh

#endif
