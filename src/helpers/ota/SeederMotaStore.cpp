#include "SeederMotaStore.h"

#if defined(OTA_SUPERSEEDER)

#include "OtaByteIO.h"
#include "OtaFormat.h"
#include "SeederFs.h"
#include <string.h>

namespace mesh {
namespace ota {

void SeederMotaStore::partPath(char* out, size_t cap) const {
  SeederFs::instance().midPath(_mid, out, cap, ".mota.part");
}

void SeederMotaStore::clear() { _total = 0; }

bool SeederMotaStore::begin(uint32_t total_size) {
  clear();
  if (total_size < 13) return false;
  char path[48];
  partPath(path, sizeof path);
  if (!SeederFs::instance().createFilled(path, total_size, 0xFF)) return false;
  _total = total_size;
  return true;
}

bool SeederMotaStore::write(uint32_t off, const uint8_t* data, uint32_t len) {
  if ((uint64_t)off + len > _total || !data) return false;
  char path[48];
  partPath(path, sizeof path);
  return SeederFs::instance().writeAt(path, off, data, len);
}

bool SeederMotaStore::read(uint32_t off, uint8_t* buf, uint32_t len) const {
  if ((uint64_t)off + len > _total || !buf) return false;
  char path[48];
  partPath(path, sizeof path);
  return SeederFs::instance().readAt(path, off, buf, len);
}

void SeederMotaStore::finalize() {
  char part[48], final_path[48];
  partPath(part, sizeof part);
  SeederFs::instance().midPath(_mid, final_path, sizeof final_path, ".mota");
  SeederFs::instance().renameFile(part, final_path);
  _total = 0;
}

bool SeederMotaStore::reopen() {
  clear();
  if (!SeederFs::instance().mounted()) return false;
  char part[48];
  partPath(part, sizeof part);
  if (!SeederFs::instance().exists(part)) return false;
  uint32_t sz = SeederFs::instance().fileSize(part);
  if (sz < 13) {
    SeederFs::instance().removeFile(part);
    return false;
  }
  uint8_t hdr[8];
  if (!SeederFs::instance().readAt(part, 0, hdr, 8) || memcmp(hdr, MOTA_MAGIC, 4) != 0) {
    return false;
  }
  uint32_t total = rd_u32le(hdr + 4);
  if (total != sz || total < 13) return false;
  _total = total;
  return true;
}

}  // namespace ota
}  // namespace mesh

#endif
