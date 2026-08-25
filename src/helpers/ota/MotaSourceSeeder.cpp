#include "MotaSourceSeeder.h"

#if defined(OTA_SEEDER_STORAGE)

#include "MotaContainer.h"
#include "OtaByteIO.h"
#include "OtaFormat.h"
#include "SeederAllowlist.h"
#include "SeederFs.h"
#include <string.h>

namespace mesh {
namespace ota {

void MotaSourceSeeder::refresh() {
  _count = 0;
  _total_bytes = 0;
  if (!SeederFs::instance().mounted()) return;

  // Static scratch — avoid a 2.5 KB stack frame on nRF52 (single-threaded loop is fine).
  static char paths[MAX_FILES][40];
  static uint32_t sizes[MAX_FILES];
  uint8_t n = 0;
  struct Collect {
    char (*paths)[40];
    uint32_t* sizes;
    uint8_t* n;
  } col{paths, sizes, &n};

  SeederFs::instance().forEachMota(
      [](const char* path, uint32_t size, void* v) -> bool {
        auto* c = (Collect*)v;
        if (*c->n >= MAX_FILES) return false;
        strncpy(c->paths[*c->n], path, 39);
        c->paths[*c->n][39] = 0;
        c->sizes[*c->n] = size;
        (*c->n)++;
        return true;
      },
      &col);

  static const SeederAllowlist k_allow_all;  // empty = admit all targets
  const SeederAllowlist& allow = _allow ? *_allow : k_allow_all;

  for (uint8_t i = 0; i < n && _count < MAX_FILES; i++) {
    MotaDesc d;
    if (!describePath(paths[i], d)) continue;
    if (!ota_cache_admit(allow, d.target_id, d.codec_id, d.flags)) continue;
    memcpy(_paths[_count], paths[i], sizeof _paths[_count]);
    _total_bytes += sizes[i] ? sizes[i] : d.total_size;
    _count++;
  }
}

bool MotaSourceSeeder::hasMid(const uint8_t mid[4]) const {
  return SeederFs::instance().hasMota(mid);
}

bool MotaSourceSeeder::describePath(const char* path, MotaDesc& out) const {
  uint8_t hdr[8];
  if (!SeederFs::instance().readAt(path, 0, hdr, 8) || memcmp(hdr, MOTA_MAGIC, 4) != 0) return false;
  uint32_t total = rd_u32le(hdr + 4);
  uint32_t sz = SeederFs::instance().fileSize(path);
  if (total != sz || total < 13) return false;
  uint8_t mf[MOTA_MFL];
  if (!SeederFs::instance().readAt(path, 8, mf, MOTA_MFL)) return false;
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

bool MotaSourceSeeder::describe(uint8_t idx, MotaDesc& out) {
  if (idx >= _count) return false;
  return describePath(_paths[idx], out);
}

bool MotaSourceSeeder::read(uint8_t idx, uint32_t off, uint8_t* buf, uint32_t len) {
  if (idx >= _count || !buf || len == 0) return false;
  return SeederFs::instance().readAt(_paths[idx], off, buf, len);
}

}  // namespace ota
}  // namespace mesh

#endif
