#pragma once

#if defined(OTA_SUPERSEEDER)

#include <stddef.h>
#include <stdint.h>

#ifndef OTA_SEEDER_DIR
#define OTA_SEEDER_DIR "/motas"
#endif

#if defined(OTA_SUPERSEEDER_QSPI)
#define OTA_SEEDER_MEDIA "NOR"
#elif defined(OTA_SUPERSEEDER_SD)
#define OTA_SEEDER_MEDIA "SD"
#else
#error "OTA_SUPERSEEDER requires OTA_SUPERSEEDER_SD or OTA_SUPERSEEDER_QSPI"
#endif

namespace mesh {
namespace ota {

// External FS mount + path helpers for the superseeder library (SD or QSPI LittleFS).
// File I/O is open-per-call so QSPI (Adafruit LittleFS single-open) can serve while capturing.
class SeederFs {
public:
  static SeederFs& instance();

  bool mount();
  bool mounted() const { return _mounted; }

  void midPath(const uint8_t mid[4], char* out, size_t cap, const char* suffix) const;
  bool hasMota(const uint8_t mid[4]) const;
  bool exists(const char* path) const;
  bool removeFile(const char* path);
  bool renameFile(const char* from, const char* to);
  uint32_t fileSize(const char* path) const;

  // Create/truncate path and fill with `fill` (0xFF matches erased-flash semantics for progress bits).
  bool createFilled(const char* path, uint32_t size, uint8_t fill = 0xFF);
  bool writeAt(const char* path, uint32_t off, const uint8_t* data, uint32_t len);
  bool readAt(const char* path, uint32_t off, uint8_t* buf, uint32_t len) const;

  // List complete `*.mota` files under OTA_SEEDER_DIR (skips `.part`).
  // cb returns false to stop. path is OTA_SEEDER_DIR/<name>.
  using MotasCb = bool (*)(const char* path, uint32_t size, void* ctx);
  void forEachMota(MotasCb cb, void* ctx) const;

private:
  SeederFs() = default;
  bool _mounted = false;
};

}  // namespace ota
}  // namespace mesh

#endif
