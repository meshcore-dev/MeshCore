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

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id) {
  char filename[40];
  char tmpfile[sizeof(filename) + sizeof(".tmp")];
  sprintf(filename, "%s/%s.id", _dir, name);
  sprintf(tmpfile, "%s.tmp", filename);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(tmpfile);
  File file = _fs->open(tmpfile, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(tmpfile, "w");
#else
  File file = _fs->open(tmpfile, "w", true);
#endif
  if (file) {
    bool ok = id.writeTo(file);
    file.close();
    if (ok) {
      _fs->rename(tmpfile, filename);
    } else {
      _fs->remove(tmpfile);
    }
    MESH_DEBUG_PRINTLN("IdentityStore::save() write - %s", ok ? "OK" : "Err");
    return ok;
  }
  MESH_DEBUG_PRINTLN("IdentityStore::save() failed");
  return false;
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id, const char display_name[]) {
  char filename[40];
  char tmpfile[sizeof(filename) + sizeof(".tmp")];
  sprintf(filename, "%s/%s.id", _dir, name);
  sprintf(tmpfile, "%s.tmp", filename);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(tmpfile);
  File file = _fs->open(tmpfile, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(tmpfile, "w");
#else
  File file = _fs->open(tmpfile, "w", true);
#endif
  if (file) {
    bool ok = id.writeTo(file);

    uint8_t tmp[32];
    memset(tmp, 0, sizeof(tmp));
    int n = strlen(display_name);
    if (n > sizeof(tmp)-1) n = sizeof(tmp)-1;
    memcpy(tmp, display_name, n);
    ok = ok && (file.write(tmp, sizeof(tmp)) == sizeof(tmp));

    file.close();
    if (ok) {
      _fs->rename(tmpfile, filename);
    } else {
      _fs->remove(tmpfile);
    }
    return ok;
  }
  return false;
}
