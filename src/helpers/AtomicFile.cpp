#include "AtomicFile.h"

#ifdef MESH_ATOMIC_FS_WRITES

#include <string.h>

namespace AtomicFile {

bool tmpName(const char* path, char* dest, size_t dest_size) {
  size_t n = strlen(path);
  if (n + 5 > dest_size) return false;
  memcpy(dest, path, n);
  memcpy(dest + n, ".tmp", 5);
  return true;
}

void cleanup(FILESYSTEM* fs, const char* path) {
  char tmp[48];
  if (tmpName(path, tmp, sizeof(tmp)) && fs->exists(tmp)) {
    fs->remove(tmp);
  }
}

bool publish(FILESYSTEM* fs, const char* tmp_path, const char* path, uint32_t expected_size) {
  // close() returns void and drops the filesystem's error, so check what
  // actually reached the medium.
  File f = fs->open(tmp_path, FILE_O_READ);
  if (!f) return false;
  bool same = (f.size() == expected_size);
  f.close();
  if (!same) return false;
  return fs->rename(tmp_path, path);
}

}

#endif
