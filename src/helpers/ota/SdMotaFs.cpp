#include "SdMotaFs.h"

#if defined(OTA_SD_SEEDER)

#include "Utils.h"
#include <SdFat.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

namespace mesh {
namespace ota {

SdMotaFs& SdMotaFs::instance() {
  static SdMotaFs fs;
  return fs;
}

void* SdMotaFs::sdFat() const {
  static SdFat sd;
  return &sd;
}

bool SdMotaFs::mount() {
  if (_mounted) return true;
  SdFat& sd = *(SdFat*)sdFat();
  if (!sd.begin(OTA_SD_CS, SD_SCK_MHZ(4))) return false;
  if (!sd.exists(OTA_SD_DIR)) sd.mkdir(OTA_SD_DIR);
  _mounted = true;
  return true;
}

void SdMotaFs::midPath(const uint8_t mid[4], char* out, size_t cap, const char* suffix) const {
  char hx[9];
  mesh::Utils::toHex(hx, mid, 4);
  snprintf(out, cap, "%s/%s%s", OTA_SD_DIR, hx, suffix ? suffix : "");
}

bool SdMotaFs::hasMota(const uint8_t mid[4]) const {
  if (!_mounted) return false;
  char path[48];
  midPath(mid, path, sizeof path, ".mota");
  return ((SdFat*)sdFat())->exists(path);
}

bool SdMotaFs::removeFile(const char* path) {
  if (!_mounted || !path || !path[0]) return false;
  SdFat& sd = *(SdFat*)sdFat();
  if (!sd.exists(path)) return true;
  return sd.remove(path);
}

} // namespace ota
} // namespace mesh

#endif
