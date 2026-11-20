#include "IdentityStore.h"
#include "AtomicFile.h"

bool IdentityStore::load(const char *name, mesh::LocalIdentity& id) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  AtomicFile::cleanup(_fs, filename);
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
  AtomicFile::cleanup(_fs, filename);
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

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);

  bool success = AtomicFile::write(_fs, filename, [&](File& file) {
    return id.writeTo(file);
  });
  MESH_DEBUG_PRINTLN("IdentityStore::save() write - %s", success ? "OK" : "Err");
  return success;
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id, const char display_name[]) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);

  uint8_t tmp[32];
  memset(tmp, 0, sizeof(tmp));
  int n = strlen(display_name);
  if (n > sizeof(tmp)-1) n = sizeof(tmp)-1;
  memcpy(tmp, display_name, n);

  return AtomicFile::write(_fs, filename, [&](File& file) {
    return id.writeTo(file) && (file.write(tmp, sizeof(tmp)) == sizeof(tmp));
  });
}
