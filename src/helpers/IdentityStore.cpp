#include "IdentityStore.h"

bool IdentityStore::load(const char *name, mesh::LocalIdentity& id) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  if (_fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(filename, "r");
#else
    File file = _fs->open(filename);
#endif
    if (file) {
      loaded = id.readFrom(file);
      file.close();
    }
  }
  return loaded;
}

bool IdentityStore::load(const char *name, mesh::LocalIdentity& id, char display_name[], int max_name_sz) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  if (_fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(filename, "r");
#else
    File file = _fs->open(filename);
#endif
    if (file) {
      loaded = id.readFrom(file);

      int n = max_name_sz;   // up to 32 bytes
      if (n > 32) n = 32;
      file.read((uint8_t *) display_name, n);
      display_name[n - 1] = 0;  // ensure null terminator

      file.close();
    }
  }
  return loaded;
}

// Compares two identities over all 96 bytes. prv_key is private, so the
// comparison runs over the buffer serialisation that LocalIdentity offers
// publicly. Its layout does not have to match the file, it only has to be the
// same on both sides.
static bool sameIdentity(const mesh::LocalIdentity& a, const mesh::LocalIdentity& b) {
  uint8_t ab[PRV_KEY_SIZE + PUB_KEY_SIZE];
  uint8_t bb[PRV_KEY_SIZE + PUB_KEY_SIZE];
  return a.writeTo(ab, sizeof(ab)) == sizeof(ab)
      && b.writeTo(bb, sizeof(bb)) == sizeof(bb)
      && memcmp(ab, bb, sizeof(ab)) == 0;
}

// A write that reports success is not proof that the medium took the data, so
// both overloads read the file back after closing it. File::close() is void in
// every platform branch of this file and drops the error it is handed, which
// is where the gap is.
//
// Measured against the two filesystem stacks this file talks to, with the 96
// bytes of an identity:
//
// littlefs, on nRF52 with prog_size 128 and on STM32 with prog_size 2048: a
// full filesystem and a failing erase are reported by lfs_file_write() itself,
// as LFS_ERR_NOSPC and LFS_ERR_IO. A failing program is not. The two writes
// return 32 and 64 and the error turns up in lfs_file_close(), whose result
// Adafruit_LittleFS' void close() drops.
//
// ESP32: nothing reaches the caller at write time at all. The core opens files
// fully buffered, setvbuf(_IOFBF, 4096) in libraries/FS/src/vfs_api.cpp, and
// VFSFileImpl::write() is fwrite(), so the 96 bytes stay in that buffer until
// close() calls fclose() and throws its result away. On a full filesystem
// fwrite() returns 32 and then 64 and fclose() returns -1. Both ESP32
// filesystems are affected, LittleFSImpl and SPIFFSImpl derive from VFSImpl.
//
// A read back that cannot be done at all counts as a failed save. That is
// deliberate: an identity nobody can read is of no use to the next boot.
bool IdentityStore::verifySaved(const char *name, const mesh::LocalIdentity& id) {
  mesh::LocalIdentity stored;
  if (!load(name, stored)) return false;   // file gone, empty or too short
  return sameIdentity(id, stored);
}

bool IdentityStore::verifySaved(const char *name, const mesh::LocalIdentity& id, const uint8_t expected_name[32]) {
  mesh::LocalIdentity stored;
  char stored_name[32];
  memset(stored_name, 0, sizeof(stored_name));   // load() fills at most 32 bytes
  if (!load(name, stored, stored_name, (int) sizeof(stored_name))) return false;
  if (!sameIdentity(id, stored)) return false;
  return memcmp(stored_name, expected_name, sizeof(stored_name)) == 0;
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(filename);
  File file = _fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(filename, "w");
#else
  File file = _fs->open(filename, "w", true);
#endif
  if (file) {
    bool success = id.writeTo(file);
    file.close();
    MESH_DEBUG_PRINTLN("IdentityStore::save() write - %s", success ? "OK" : "Err");
    if (success && !verifySaved(name, id)) {
      MESH_DEBUG_PRINTLN("IdentityStore::save() verify - Err");
      success = false;
    }
    return success;
  }
  MESH_DEBUG_PRINTLN("IdentityStore::save() failed");
  return false;
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id, const char display_name[]) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(filename);
  File file = _fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(filename, "w");
#else
  File file = _fs->open(filename, "w", true);
#endif
  if (file) {
    bool success = id.writeTo(file);

    uint8_t tmp[32];
    memset(tmp, 0, sizeof(tmp));
    int n = strlen(display_name);
    if (n > sizeof(tmp)-1) n = sizeof(tmp)-1;
    memcpy(tmp, display_name, n);
    success = success && (file.write(tmp, sizeof(tmp)) == sizeof(tmp));

    file.close();
    MESH_DEBUG_PRINTLN("IdentityStore::save() write - %s", success ? "OK" : "Err");
    if (success && !verifySaved(name, id, tmp)) {
      MESH_DEBUG_PRINTLN("IdentityStore::save() verify - Err");
      success = false;
    }
    return success;
  }
  MESH_DEBUG_PRINTLN("IdentityStore::save() failed");
  return false;
}
