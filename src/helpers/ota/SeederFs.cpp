#include "SeederFs.h"

#if defined(OTA_SEEDER_STORAGE)

#include "Utils.h"
#include <stdio.h>
#include <string.h>

#if defined(OTA_SEEDER_STORAGE_SD)
#include <SdFat.h>
#include <SPI.h>
#ifndef OTA_SD_CS
#define OTA_SD_CS SS
#endif
#elif defined(OTA_SEEDER_STORAGE_QSPI)
#include <CustomLFS_QSPIFlash.h>
#endif

namespace mesh {
namespace ota {

SeederFs& SeederFs::instance() {
  static SeederFs fs;
  return fs;
}

#if defined(OTA_SEEDER_STORAGE_SD)

static SdFat& sdRef() {
  static SdFat sd;
  return sd;
}

bool SeederFs::mount() {
  if (_mounted) return true;
  if (!sdRef().begin(OTA_SD_CS, SD_SCK_MHZ(4))) return false;
  if (!sdRef().exists(OTA_SEEDER_DIR)) sdRef().mkdir(OTA_SEEDER_DIR);
  _mounted = true;
  return true;
}

bool SeederFs::exists(const char* path) const {
  return _mounted && path && sdRef().exists(path);
}

bool SeederFs::removeFile(const char* path) {
  if (!_mounted || !path || !path[0]) return false;
  if (!sdRef().exists(path)) return true;
  return sdRef().remove(path);
}

bool SeederFs::renameFile(const char* from, const char* to) {
  if (!_mounted || !from || !to) return false;
  sdRef().remove(to);
  return sdRef().rename(from, to);
}

uint32_t SeederFs::fileSize(const char* path) const {
  if (!_mounted || !path) return 0;
  File32 f = sdRef().open(path, O_RDONLY);
  if (!f) return 0;
  uint32_t n = (uint32_t)f.size();
  f.close();
  return n;
}

bool SeederFs::createFilled(const char* path, uint32_t size, uint8_t fill) {
  if (!_mounted || !path) return false;
  sdRef().remove(path);
  File32 f;
  if (!f.open(path, O_RDWR | O_CREAT | O_TRUNC)) return false;
  static uint8_t buf[512];
  memset(buf, fill, sizeof buf);
  uint32_t done = 0;
  while (done < size) {
    uint32_t chunk = size - done;
    if (chunk > sizeof buf) chunk = sizeof buf;
    if (f.write(buf, chunk) != (int)chunk) {
      f.close();
      sdRef().remove(path);
      return false;
    }
    done += chunk;
  }
  f.close();
  return true;
}

bool SeederFs::writeAt(const char* path, uint32_t off, const uint8_t* data, uint32_t len) {
  if (!_mounted || !path || !data) return false;
  File32 f;
  if (!f.open(path, O_RDWR)) return false;
  if (!f.seek(off) || f.write(data, len) != (int)len) {
    f.close();
    return false;
  }
  f.close();
  return true;
}

bool SeederFs::readAt(const char* path, uint32_t off, uint8_t* buf, uint32_t len) const {
  if (!_mounted || !path || !buf) return false;
  File32 f = sdRef().open(path, O_RDONLY);
  if (!f) return false;
  if (!f.seek(off) || f.read(buf, len) != (int)len) {
    f.close();
    return false;
  }
  f.close();
  return true;
}

void SeederFs::forEachMota(MotasCb cb, void* ctx) const {
  if (!_mounted || !cb) return;
  File32 dir = sdRef().open(OTA_SEEDER_DIR);
  if (!dir || !dir.isDirectory()) return;
  File32 ent;
  while (ent.openNext(&dir, O_RDONLY)) {
    char name[32];
    ent.getName(name, sizeof name);
    uint32_t sz = (uint32_t)ent.size();
    ent.close();
    size_t nlen = strlen(name);
    if (nlen < 6 || strcmp(name + nlen - 5, ".mota") != 0) continue;
    if (strstr(name, ".part")) continue;
    char path[48];
    snprintf(path, sizeof path, "%s/%s", OTA_SEEDER_DIR, name);
    if (!cb(path, sz, ctx)) break;
  }
  dir.close();
}

#elif defined(OTA_SEEDER_STORAGE_QSPI)

bool SeederFs::mount() {
  if (_mounted) return true;
  if (!QSPIFlash.begin()) return false;
  if (!QSPIFlash.exists(OTA_SEEDER_DIR)) QSPIFlash.mkdir(OTA_SEEDER_DIR);
  _mounted = true;
  return true;
}

bool SeederFs::exists(const char* path) const {
  return _mounted && path && QSPIFlash.exists(path);
}

bool SeederFs::removeFile(const char* path) {
  if (!_mounted || !path || !path[0]) return false;
  if (!QSPIFlash.exists(path)) return true;
  return QSPIFlash.remove(path);
}

bool SeederFs::renameFile(const char* from, const char* to) {
  if (!_mounted || !from || !to) return false;
  QSPIFlash.remove(to);
  return QSPIFlash.rename(from, to);
}

uint32_t SeederFs::fileSize(const char* path) const {
  if (!_mounted || !path) return 0;
  File f = QSPIFlash.open(path, FILE_O_READ);
  if (!f) return 0;
  uint32_t n = f.size();
  f.close();
  return n;
}

bool SeederFs::createFilled(const char* path, uint32_t size, uint8_t fill) {
  if (!_mounted || !path) return false;
  QSPIFlash.remove(path);
  File f = QSPIFlash.open(path, FILE_O_WRITE);
  if (!f) return false;
  static uint8_t buf[512];
  memset(buf, fill, sizeof buf);
  uint32_t done = 0;
  while (done < size) {
    uint32_t chunk = size - done;
    if (chunk > sizeof buf) chunk = sizeof buf;
    if (f.write(buf, chunk) != chunk) {
      f.close();
      QSPIFlash.remove(path);
      return false;
    }
    done += chunk;
  }
  f.close();
  return true;
}

bool SeederFs::writeAt(const char* path, uint32_t off, const uint8_t* data, uint32_t len) {
  if (!_mounted || !path || !data) return false;
  File f = QSPIFlash.open(path, FILE_O_WRITE);
  if (!f) return false;
  // Adafruit FILE_O_WRITE creates/truncates — reopen without truncate by seeking after open
  // of an existing filled part file. Use read+rewrite via LFS: open WRITE on existing keeps content
  // when using seek (Adafruit opens LFS_O_RDWR|LFS_O_CREAT without TRUNC for FILE_O_WRITE on some
  // versions). Guard: if size collapsed, fail.
  if (!f.seek(off)) {
    f.close();
    return false;
  }
  uint32_t done = 0;
  while (done < len) {
    uint32_t chunk = len - done;
    if (chunk > 512) chunk = 512;
    if (f.write(data + done, chunk) != chunk) {
      f.close();
      return false;
    }
    done += chunk;
  }
  f.close();
  return true;
}

bool SeederFs::readAt(const char* path, uint32_t off, uint8_t* buf, uint32_t len) const {
  if (!_mounted || !path || !buf) return false;
  File f = QSPIFlash.open(path, FILE_O_READ);
  if (!f) return false;
  if (!f.seek(off)) {
    f.close();
    return false;
  }
  uint32_t done = 0;
  while (done < len) {
    uint32_t chunk = len - done;
    if (chunk > 512) chunk = 512;
    int n = f.read(buf + done, (uint16_t)chunk);
    if (n != (int)chunk) {
      f.close();
      return false;
    }
    done += chunk;
  }
  f.close();
  return true;
}

void SeederFs::forEachMota(MotasCb cb, void* ctx) const {
  if (!_mounted || !cb) return;
  File dir = QSPIFlash.open(OTA_SEEDER_DIR, FILE_O_READ);
  if (!dir || !dir.isDirectory()) return;

  // Collect names first — Adafruit LittleFS allows only one open file at a time.
  char names[64][28];
  uint8_t n = 0;
  while (n < 64) {
    File ent = dir.openNextFile(FILE_O_READ);
    if (!ent) break;
    const char* name = ent.name();
    bool ok = name && strlen(name) >= 6 && strcmp(name + strlen(name) - 5, ".mota") == 0 &&
              !strstr(name, ".part");
    if (ok) {
      strncpy(names[n], name, sizeof names[n] - 1);
      names[n][sizeof names[n] - 1] = 0;
      n++;
    }
    ent.close();
  }
  dir.close();

  for (uint8_t i = 0; i < n; i++) {
    char path[48];
    snprintf(path, sizeof path, "%s/%s", OTA_SEEDER_DIR, names[i]);
    uint32_t sz = fileSize(path);
    if (!cb(path, sz, ctx)) break;
  }
}

#endif

void SeederFs::midPath(const uint8_t mid[4], char* out, size_t cap, const char* suffix) const {
  char hx[9];
  mesh::Utils::toHex(hx, mid, 4);
  snprintf(out, cap, "%s/%s%s", OTA_SEEDER_DIR, hx, suffix ? suffix : "");
}

bool SeederFs::hasMota(const uint8_t mid[4]) const {
  if (!_mounted) return false;
  char path[48];
  midPath(mid, path, sizeof path, ".mota");
  return exists(path);
}

}  // namespace ota
}  // namespace mesh

#endif
