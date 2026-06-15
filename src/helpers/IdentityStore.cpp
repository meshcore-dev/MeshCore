#include "IdentityStore.h"

bool IdentityStore::load(const char *name, mesh::LocalIdentity& id) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  if (_fs->exists(filename)) {
    loaded = loadFromFS(_fs, filename, id);
  }
  if (_fsBackup && _fsBackup->exists(filename) && !loaded) {
    loaded = loadFromFS(_fsBackup, filename, id);
    MESH_DEBUG_PRINTLN("Loading identity from backup %s", loaded ? "successful" : "failed");
    if (loaded) saveToFS(_fs, filename, id);
  }
  if (_fsBackup && !_fsBackup->exists(filename) && loaded) {
    saveToFS(_fsBackup, filename, id);
  }
  return loaded;
}

bool IdentityStore::load(const char *name, mesh::LocalIdentity& id, char display_name[], int max_name_sz) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  if (_fs->exists(filename)) {
    loaded = loadFromFS(_fs, filename, id, display_name, max_name_sz);
  }
  if (_fsBackup && _fsBackup->exists(filename) && !loaded) {
    loaded = loadFromFS(_fsBackup, filename, id, display_name, max_name_sz);
    MESH_DEBUG_PRINTLN("Loading identity from backup %s", loaded ? "successful" : "failed");
    if (loaded) { saveToFS(_fs, filename, id, display_name); }
  }
  if (_fsBackup && !_fsBackup->exists(filename) && loaded) {
    saveToFS(_fsBackup, filename, id, display_name);
  }
  return loaded;
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  bool success = saveToFS(_fs, filename, id);
  if (_fsBackup) { saveToFS(_fsBackup, filename, id); }
  return success;
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id, const char display_name[]) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  bool success = saveToFS(_fs, filename, id, display_name);
  if (_fsBackup) { saveToFS(_fsBackup, filename, id, display_name); }
  return success;
}

bool IdentityStore::loadFromFS(FILESYSTEM* fs, const char* filename, mesh::LocalIdentity& id) {
  bool loaded = false;
#if defined(RP2040_PLATFORM)
    File file = fs->open(filename, "r");
#else
    File file = fs->open(filename);
#endif
    if (file) {
      loaded = id.readFrom(file);
      file.close();
    }
  return loaded;
}

bool IdentityStore::loadFromFS(FILESYSTEM* fs, const char* filename, mesh::LocalIdentity& id, char display_name[], int max_name_sz) {
  bool loaded = false;
#if defined(RP2040_PLATFORM)
    File file = fs->open(filename, "r");
#else
    File file = fs->open(filename);
#endif
    if (file) {
      loaded = id.readFrom(file);

      int n = max_name_sz;   // up to 32 bytes
      if (n > 32) n = 32;
      file.read((uint8_t *) display_name, n);
      display_name[n - 1] = 0;  // ensure null terminator

      file.close();
    }
  return loaded;
}

bool IdentityStore::saveToFS(FILESYSTEM* fs, const char* filename, const mesh::LocalIdentity& id) {
  char tempfilename[44];
  sprintf(tempfilename, "%s.tmp", filename);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(tempfilename);
  File file = fs->open(tempfilename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open(tempfilename, "w");
#else
  File file = fs->open(tempfilename, "w", true);
#endif
  if (file) {
    bool success = id.writeTo(file);
    file.close();
    if (!success) { fs->remove(tempfilename);
      MESH_DEBUG_PRINTLN("IdentityStore::save() write failed");
      return false;
    }
    #if defined(ESP32_PLATFORM)
    fs->remove(filename); // SPIFFS doesn't support atomic rename, must remove old file first
    success = fs->rename(tempfilename, filename); 
    #else
    success = fs->rename(tempfilename, filename);
    #endif
    if (!success) { 
      fs->remove(tempfilename);
      MESH_DEBUG_PRINTLN("IdentityStore::save() rename failed");
      return false;
    }
    MESH_DEBUG_PRINTLN("IdentityStore::save() write - OK");
    return success;
  }
  MESH_DEBUG_PRINTLN("IdentityStore::save() failed to open file for writing");
  return false;
}

bool IdentityStore::saveToFS(FILESYSTEM* fs, const char* filename, const mesh::LocalIdentity& id, const char display_name[]) {
  char tempfilename[44];
  sprintf(tempfilename, "%s.tmp", filename);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(tempfilename);
  File file = fs->open(tempfilename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open(tempfilename, "w");
#else
  File file = fs->open(tempfilename, "w", true);
#endif
  if (file) {
    id.writeTo(file);

    uint8_t tmp[32];
    memset(tmp, 0, sizeof(tmp));
    int n = strlen(display_name);
    if (n > sizeof(tmp)-1) n = sizeof(tmp)-1;
    memcpy(tmp, display_name, n);
    file.write(tmp, sizeof(tmp));

    file.close();
    #if defined(ESP32_PLATFORM)
    if (fs->exists(filename)) fs->remove(filename); 
    #endif
    fs->rename(tempfilename, filename);
    return true;
  }
  return false;

}




