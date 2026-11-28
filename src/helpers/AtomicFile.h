#pragma once

// Crash-safe replacement for the "remove(file), then open and write" pattern.
//
// On littlefs platforms (nRF52, STM32) the new content is written to
// "<path>.tmp", closed, read back for a size check and only then renamed over
// <path>. lfs_rename() replaces an existing target in one commit, so a power
// loss leaves either the old or the new file, never a missing or short one.
// The old file is never removed before the new one is complete.
//
// The temp copy needs free space for a second copy of the file. If it does not
// fit (or the write or rename fails), write() falls back to the previous
// behaviour: remove the old file and write <path> in place.
//
// Other platforms (ESP32, RP2040) keep the previous behaviour unchanged. Define
// MESH_ATOMIC_FS_WRITES to switch the atomic path on there once rename() over
// an existing file is known to work on that filesystem.

#include <stddef.h>
#include <stdint.h>

#if defined(ESP32) || defined(RP2040_PLATFORM)
  #include <FS.h>
  #ifndef FILESYSTEM
    #define FILESYSTEM  fs::FS
  #endif
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <Adafruit_LittleFS.h>
  #ifndef FILESYSTEM
    #define FILESYSTEM  Adafruit_LittleFS
  #endif
  using namespace Adafruit_LittleFS_Namespace;
#endif

#if (defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)) && !defined(MESH_ATOMIC_FS_WRITES)
  #define MESH_ATOMIC_FS_WRITES 1
#endif

namespace AtomicFile {

#ifdef MESH_ATOMIC_FS_WRITES
// Removes a "<path>.tmp" left behind by an interrupted write(). The file at
// <path> is never half-written, so an orphaned temp copy is always an
// unfinished one and is never adopted. Call before loading <path>.
void cleanup(FILESYSTEM* fs, const char* path);
// Size-checks the closed temp file and renames it over <path>.
bool publish(FILESYSTEM* fs, const char* tmp_path, const char* path, uint32_t expected_size);
bool tmpName(const char* path, char* dest, size_t dest_size);
#else
inline void cleanup(FILESYSTEM*, const char*) { }
#endif

inline File openPlain(FILESYSTEM* fs, const char* path) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(path);
  return fs->open(path, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(path, "w");
#else
  return fs->open(path, "w", true);
#endif
}

// Runs body(File&) against a fresh file and makes it <path> when body() returns
// true. body() may be called a second time (fallback), so it must start from
// scratch each time. Returns body()'s result, false if nothing could be saved.
template <class Body>
bool write(FILESYSTEM* fs, const char* path, Body body) {
#ifdef MESH_ATOMIC_FS_WRITES
  char tmp[48];
  if (tmpName(path, tmp, sizeof(tmp))) {
    fs->remove(tmp);
    File f = fs->open(tmp, FILE_O_WRITE);
    if (f) {
      bool ok = body(f);
      uint32_t written = f.position();
      f.close();
      if (ok && publish(fs, tmp, path, written)) return true;
    }
    fs->remove(tmp);   // old file is untouched, now try the in-place way
  }
#endif
  File f = openPlain(fs, path);
  if (!f) return false;
  bool ok = body(f);
  f.close();
  return ok;
}

}
