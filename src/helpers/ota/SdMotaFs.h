#pragma once

#if defined(OTA_SD_SEEDER)

#include <stdint.h>
#include <stddef.h>

#ifndef OTA_SD_CS
#define OTA_SD_CS SS
#endif
#ifndef OTA_SD_DIR
#define OTA_SD_DIR "/motas"
#endif

namespace mesh {
namespace ota {

// Shared SD mount + path helpers for the superseeder (MotaSourceSd + SdMotaStore).
class SdMotaFs {
public:
  static SdMotaFs& instance();

  bool mount();
  bool mounted() const { return _mounted; }

  // Build OTA_SD_DIR/<midhex><suffix> (e.g. ".mota" or ".mota.part").
  void midPath(const uint8_t mid[4], char* out, size_t cap, const char* suffix) const;

  bool hasMota(const uint8_t mid[4]) const;
  bool removeFile(const char* path);

  void* sdFat() const;   // opaque SdFat* for .cpp (keeps SdFat out of other headers)

private:
  SdMotaFs() = default;
  bool _mounted = false;
};

} // namespace ota
} // namespace mesh

#endif
