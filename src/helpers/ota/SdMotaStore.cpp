#include "SdMotaStore.h"

#if defined(OTA_SD_SEEDER)

#include "SdMotaFs.h"
#include "OtaByteIO.h"
#include "OtaFormat.h"
#include <SdFat.h>
#include <string.h>

namespace mesh {
namespace ota {

void SdMotaStore::partPath(char* out, size_t cap) const {
  SdMotaFs::instance().midPath(_mid, out, cap, ".mota.part");
}

void SdMotaStore::clear() {
  if (_file) { ((File32*)_file)->close(); delete (File32*)_file; _file = nullptr; }
  _total = 0;
}

bool SdMotaStore::openPart(bool create, uint32_t fill_size) {
  if (!SdMotaFs::instance().mounted()) return false;
  if (_file) { ((File32*)_file)->close(); delete (File32*)_file; _file = nullptr; }
  char path[48];
  partPath(path, sizeof path);
  SdFat& sd = *(SdFat*)SdMotaFs::instance().sdFat();
  File32* f = new File32();
  if (create) {
    sd.remove(path);
    if (!f->open(path, O_RDWR | O_CREAT | O_TRUNC)) { delete f; return false; }
    static uint8_t ff[512];
    static bool ff_init = false;
    if (!ff_init) { memset(ff, 0xFF, sizeof ff); ff_init = true; }
    uint32_t done = 0;
    while (done < fill_size) {
      uint32_t chunk = fill_size - done;
      if (chunk > sizeof ff) chunk = sizeof ff;
      if (f->write(ff, chunk) != (int)chunk) { f->close(); delete f; sd.remove(path); return false; }
      done += chunk;
    }
  } else {
    if (!f->open(path, O_RDWR)) { delete f; return false; }
  }
  _file = f;
  return true;
}

bool SdMotaStore::begin(uint32_t total_size) {
  clear();
  if (total_size < 13) return false;
  if (!openPart(true, total_size)) return false;
  _total = total_size;
  return true;
}

bool SdMotaStore::write(uint32_t off, const uint8_t* data, uint32_t len) {
  if (!_file || (uint64_t)off + len > _total || !data) return false;
  File32& f = *(File32*)_file;
  if (!f.seek(off)) return false;
  return f.write(data, len) == (int)len;
}

bool SdMotaStore::read(uint32_t off, uint8_t* buf, uint32_t len) const {
  if (!_file || (uint64_t)off + len > _total || !buf) return false;
  File32& f = *(File32*)_file;
  if (!f.seek(off)) return false;
  return f.read(buf, len) == (int)len;
}

void SdMotaStore::finalize() {
  if (_file) { ((File32*)_file)->close(); delete (File32*)_file; _file = nullptr; }
  char part[48], final_path[48];
  partPath(part, sizeof part);
  SdMotaFs::instance().midPath(_mid, final_path, sizeof final_path, ".mota");
  SdFat& sd = *(SdFat*)SdMotaFs::instance().sdFat();
  sd.remove(final_path);
  sd.rename(part, final_path);
  _total = 0;
}

bool SdMotaStore::reopen() {
  clear();
  if (!SdMotaFs::instance().mounted()) return false;
  char part[48];
  partPath(part, sizeof part);
  SdFat& sd = *(SdFat*)SdMotaFs::instance().sdFat();
  if (!sd.exists(part)) return false;
  if (!openPart(false)) return false;
  File32& f = *(File32*)_file;
  uint32_t sz = (uint32_t)f.size();
  if (sz < 13) { clear(); SdMotaFs::instance().removeFile(part); return false; }
  uint8_t hdr[8];
  if (f.read(hdr, 8) != 8 || memcmp(hdr, MOTA_MAGIC, 4) != 0) { clear(); return false; }
  uint32_t total = rd_u32le(hdr + 4);
  if (total != sz || total < 13) { clear(); return false; }
  f.seek(0);
  _total = total;
  return true;
}

} // namespace ota
} // namespace mesh

#endif
