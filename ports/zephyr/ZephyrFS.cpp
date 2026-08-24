//
// LittleFS-backed filesystem shim for MeshCore on Zephyr. See ZephyrFS.h.
//
#include <ZephyrFS.h>

#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#include <string.h>

namespace mc_zephyr_fs {

// --- Mount plumbing --------------------------------------------------------

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(meshcore_lfs);

// PARTITION_ID, not the deprecated FIXED_PARTITION_ID. Kept as a uintptr_t
// because fs_mkfs() wants the raw id while fs_mount_t wants it as a void*.
static const uintptr_t mc_storage_id = PARTITION_ID(storage_partition);

// Designator order must match the declaration order in struct fs_mount_t
// (type, mnt_point, fs_data, storage_dev); C++ rejects out-of-order designators.
static struct fs_mount_t mc_mount = {
  .type = FS_LITTLEFS,
  .mnt_point = MC_FS_MOUNT_POINT,
  .fs_data = &meshcore_lfs,
  .storage_dev = (void *)mc_storage_id,
};

/**
 * Translate a MeshCore path into a mount-point-relative one.
 * "/prefs.json" -> "/lfs/prefs.json". Returns false if it would not fit.
 */
static bool full_path(char* dest, size_t dest_sz, const char* path) {
  if (path == NULL) return false;

  const size_t mount_len = strlen(MC_FS_MOUNT_POINT);
  const size_t path_len = strlen(path);
  const bool needs_slash = (path[0] != '/');

  if (mount_len + (needs_slash ? 1 : 0) + path_len + 1 > dest_sz) return false;

  memcpy(dest, MC_FS_MOUNT_POINT, mount_len);
  size_t n = mount_len;
  if (needs_slash) dest[n++] = '/';
  memcpy(dest + n, path, path_len);
  dest[n + path_len] = '\0';
  return true;
}

// --- File ------------------------------------------------------------------

File::File() : _open(false) {
  fs_file_t_init(&_fp);
  _name[0] = '\0';
}

File::~File() {
  close();
}

File::File(File&& other) : _open(other._open) {
  _fp = other._fp;
  memcpy(_name, other._name, sizeof(_name));
  // Leave the source safe to destruct: it must not close our handle.
  other._open = false;
  fs_file_t_init(&other._fp);
  other._name[0] = '\0';
}

File& File::operator=(File&& other) {
  if (this != &other) {
    close();
    _fp = other._fp;
    _open = other._open;
    memcpy(_name, other._name, sizeof(_name));
    other._open = false;
    fs_file_t_init(&other._fp);
    other._name[0] = '\0';
  }
  return *this;
}

void File::close() {
  if (_open) {
    fs_close(&_fp);
    _open = false;
  }
}

size_t File::write(uint8_t b) {
  return write(&b, 1);
}

size_t File::write(const uint8_t* buf, size_t len) {
  if (!_open || buf == NULL) return 0;
  ssize_t n = fs_write(&_fp, buf, len);
  return (n < 0) ? 0 : (size_t)n;
}

int File::read(void* buf, size_t len) {
  if (!_open || buf == NULL) return -1;
  ssize_t n = fs_read(&_fp, buf, len);
  return (n < 0) ? -1 : (int)n;
}

int File::read() {
  uint8_t b;
  int n = read(&b, 1);
  return (n == 1) ? (int)b : -1;
}

size_t File::readBytes(uint8_t* buf, size_t len) {
  int n = read(buf, len);
  return (n < 0) ? 0 : (size_t)n;
}

int File::peek() {
  if (!_open) return -1;
  off_t pos = fs_tell(&_fp);
  if (pos < 0) return -1;
  int c = read();
  fs_seek(&_fp, pos, FS_SEEK_SET);
  return c;
}

int File::available() {
  if (!_open) return 0;
  off_t pos = fs_tell(&_fp);
  if (pos < 0) return 0;
  uint32_t total = size();
  return (total > (uint32_t)pos) ? (int)(total - (uint32_t)pos) : 0;
}

void File::flush() {
  if (_open) fs_sync(&_fp);
}

bool File::seek(uint32_t pos) {
  if (!_open) return false;
  return fs_seek(&_fp, (off_t)pos, FS_SEEK_SET) == 0;
}

uint32_t File::size() {
  if (!_open) return 0;
  off_t pos = fs_tell(&_fp);
  if (pos < 0) return 0;
  if (fs_seek(&_fp, 0, FS_SEEK_END) != 0) return 0;
  off_t end = fs_tell(&_fp);
  fs_seek(&_fp, pos, FS_SEEK_SET);
  return (end < 0) ? 0 : (uint32_t)end;
}

// --- LittleFileSystem ------------------------------------------------------

bool LittleFileSystem::begin() {
  if (_mounted) return true;

  // Zephyr's littlefs backend formats automatically when the partition does not
  // already hold a valid filesystem, unless FS_MOUNT_FLAG_NO_FORMAT is set.
  int rc = fs_mount(&mc_mount);
  if (rc == -EBUSY) {   // already mounted by someone else
    _mounted = true;
    return true;
  }
  _mounted = (rc == 0);
  return _mounted;
}

bool LittleFileSystem::end() {
  if (!_mounted) return true;
  int rc = fs_unmount(&mc_mount);
  if (rc == 0) _mounted = false;
  return rc == 0;
}

bool LittleFileSystem::format() {
  if (_mounted) {
    fs_unmount(&mc_mount);
    _mounted = false;
  }

  int rc = fs_mkfs(FS_LITTLEFS, mc_storage_id, mc_mount.fs_data, 0);
  if (rc != 0) return false;

  return begin();
}

File LittleFileSystem::open(const char* path) {
  return open(path, FILE_O_READ);
}

File LittleFileSystem::open(const char* path, uint8_t mode) {
  File f;
  char resolved[MC_FS_MAX_PATH + sizeof(MC_FS_MOUNT_POINT) + 1];

  if (!_mounted || !full_path(resolved, sizeof(resolved), path)) return f;

  fs_mode_t flags = (mode == FILE_O_WRITE)
      ? (FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC)
      : FS_O_READ;

  if (fs_open(&f._fp, resolved, flags) != 0) return f;   // stays !f

  f._open = true;
  strncpy(f._name, path, sizeof(f._name) - 1);
  f._name[sizeof(f._name) - 1] = '\0';
  return f;
}

bool LittleFileSystem::exists(const char* path) {
  char resolved[MC_FS_MAX_PATH + sizeof(MC_FS_MOUNT_POINT) + 1];
  if (!_mounted || !full_path(resolved, sizeof(resolved), path)) return false;

  struct fs_dirent entry;
  return fs_stat(resolved, &entry) == 0;
}

bool LittleFileSystem::remove(const char* path) {
  char resolved[MC_FS_MAX_PATH + sizeof(MC_FS_MOUNT_POINT) + 1];
  if (!_mounted || !full_path(resolved, sizeof(resolved), path)) return false;
  return fs_unlink(resolved) == 0;
}

bool LittleFileSystem::mkdir(const char* path) {
  char resolved[MC_FS_MAX_PATH + sizeof(MC_FS_MOUNT_POINT) + 1];
  if (!_mounted || !full_path(resolved, sizeof(resolved), path)) return false;

  int rc = fs_mkdir(resolved);
  // IdentityStore::begin() calls mkdir unconditionally on every boot, so an
  // already-present directory is success, not failure.
  return (rc == 0) || (rc == -EEXIST);
}

LittleFileSystem InternalFS;

} // namespace mc_zephyr_fs
