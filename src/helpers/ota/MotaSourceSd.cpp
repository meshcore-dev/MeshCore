#include "MotaSourceSd.h"

#if defined(OTA_SD_SEEDER)

#include "SdMotaFs.h"
#include "MotaContainer.h"
#include "OtaByteIO.h"
#include "OtaFormat.h"
#include "Utils.h"
#include <SdFat.h>
#include <string.h>

namespace mesh {
namespace ota {

void MotaSourceSd::refresh() {
  _count = 0;
  _total_bytes = 0;
  if (!SdMotaFs::instance().mounted()) return;
  SdFat& sd = *(SdFat*)SdMotaFs::instance().sdFat();
  File32 dir = sd.open(OTA_SD_DIR);
  if (!dir || !dir.isDirectory()) return;
  File32 ent;
  while (ent.openNext(&dir, O_RDONLY) && _count < MAX_FILES) {
    char name[32];
    ent.getName(name, sizeof name);
    ent.close();
    size_t nlen = strlen(name);
    if (nlen < 6 || strcmp(name + nlen - 5, ".mota") != 0) continue;
    if (strstr(name, ".part")) continue;
    snprintf(_paths[_count], sizeof _paths[_count], "%s/%s", OTA_SD_DIR, name);
    File32 f = sd.open(_paths[_count]);
    if (f) { _total_bytes += (uint32_t)f.size(); f.close(); }
    _count++;
  }
  dir.close();
}

bool MotaSourceSd::hasMid(const uint8_t mid[4]) const {
  return SdMotaFs::instance().hasMota(mid);
}

bool MotaSourceSd::describePath(const char* path, MotaDesc& out) const {
  SdFat& sd = *(SdFat*)SdMotaFs::instance().sdFat();
  File32 f = sd.open(path, O_RDONLY);
  if (!f) return false;
  uint32_t total = (uint32_t)f.size();
  if (total < 13) { f.close(); return false; }
  uint8_t hdr[8];
  if (f.read(hdr, 8) != 8 || memcmp(hdr, MOTA_MAGIC, 4) != 0) { f.close(); return false; }
  if (rd_u32le(hdr + 4) != total) { f.close(); return false; }
  uint8_t mf[MOTA_MFL];
  if (f.read(mf, MOTA_MFL) != MOTA_MFL) { f.close(); return false; }
  f.close();
  MotaManifest m;
  if (!mota_parse_manifest(mf, MOTA_MFL, m)) return false;
  memcpy(out.mid, m.merkle_root, 4);
  out.target_id = m.target_id;
  out.fw_version = m.fw_version;
  out.codec_id = m.codec_id;
  out.flags = m.flags;
  out.total_size = total;
  out.leaves_off = 8 + MOTA_MFL;
  out.block_count = m.block_count;
  out.payload_off = out.leaves_off + m.block_count * 4;
  out.payload_size = m.payload_size;
  return true;
}

bool MotaSourceSd::describe(uint8_t idx, MotaDesc& out) {
  if (idx >= _count) return false;
  return describePath(_paths[idx], out);
}

bool MotaSourceSd::read(uint8_t idx, uint32_t off, uint8_t* buf, uint32_t len) {
  if (idx >= _count || !buf || len == 0) return false;
  SdFat& sd = *(SdFat*)SdMotaFs::instance().sdFat();
  File32 f = sd.open(_paths[idx], O_RDONLY);
  if (!f) return false;
  if (!f.seek(off)) { f.close(); return false; }
  int n = f.read(buf, len);
  f.close();
  return n == (int)len;
}

} // namespace ota
} // namespace mesh

#endif
