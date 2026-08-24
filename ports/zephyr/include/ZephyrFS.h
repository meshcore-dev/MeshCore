#pragma once
//
// LittleFS-backed filesystem shim for MeshCore on Zephyr.
//
// MeshCore has no mesh::FileSystem interface: src/helpers/IdentityStore.h picks a
// concrete type by macro (fs::FS on ESP32/RP2040, Adafruit_LittleFS on nRF52/STM32)
// and callers duck-type against it. This provides the same call shapes as the
// nRF52/STM32 path -- open(path), open(path, FILE_O_WRITE), exists, remove, mkdir,
// format -- so those call sites only need their #if condition widened rather than a
// whole new branch.
//
// Storage is LittleFS on the `storage_partition` fixed partition, which exists both
// on native_sim and on the ESP32-S3/nRF52 boards MeshCore targets.
//
// Paths: MeshCore hardcodes absolute paths such as "/prefs.json"
// (src/helpers/CommonCLI.cpp:145). Zephyr resolves paths against a registered mount
// point, so this shim prepends MC_FS_MOUNT_POINT internally and callers stay
// unchanged.

#include <Stream.h>

#include <zephyr/fs/fs.h>

#include <stddef.h>
#include <stdint.h>

// Open modes, matching the Adafruit_LittleFS spelling used by the code path this
// shim stands in for.
#ifndef FILE_O_READ
#define FILE_O_READ   0
#endif
#ifndef FILE_O_WRITE
#define FILE_O_WRITE  1
#endif

#ifndef MC_FS_MOUNT_POINT
#define MC_FS_MOUNT_POINT "/lfs"
#endif

// Longest path MeshCore builds is IdentityStore's `char filename[40]`.
#define MC_FS_MAX_PATH 64

namespace mc_zephyr_fs {

/**
 * \brief  An open file. Derives from Stream because mesh::Identity::readFrom() and
 *     writeTo() (src/Identity.cpp:42-48) serialise through a Stream&, as does
 *     ConfigSerializer::loadSerial/saveSerial.
 *
 * Move-only. Every MeshCore call site is `File f = fs->open(...)` or a direct
 * `return fs->open(...)`, both of which are guaranteed copy elision in C++17, so
 * nothing needs to copy one. Deleting the copy turns any future copy into a compile
 * error rather than a double-close on the underlying fs_file_t.
 */
class File : public Stream {
public:
  File();
  ~File();

  File(File&& other);
  File& operator=(File&& other);
  File(const File&) = delete;
  File& operator=(const File&) = delete;

  /** \returns true if the file is open. Callers use `if (file) { ... }`. */
  explicit operator bool() const { return _open; }

  // --- Print / Stream ------------------------------------------------------
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buf, size_t len) override;
  using Print::write;                 // keep write(const char*)

  int read() override;
  int peek() override;
  int available() override;
  void flush() override;
  size_t readBytes(uint8_t* buf, size_t len) override;
  using Stream::readBytes;            // keep readBytes(char*, size_t)

  // --- Arduino File surface ------------------------------------------------
  /** \brief Bulk read. \returns bytes read, or -1 on error. */
  int read(void* buf, size_t len);
  bool seek(uint32_t pos);
  uint32_t size();
  void close();
  const char* name() const { return _name; }
  bool isDirectory() const { return false; }

private:
  friend class LittleFileSystem;

  struct fs_file_t _fp;
  bool _open;
  char _name[MC_FS_MAX_PATH];   // the path as MeshCore passed it, un-prefixed
};

/**
 * \brief  The filesystem itself. One instance, `InternalFS` below.
 */
class LittleFileSystem {
public:
  /**
   * \brief  Mount the storage partition, formatting it first if it does not hold a
   *     valid LittleFS. Safe to call more than once.
   * \returns true if the filesystem is mounted.
   */
  bool begin();

  /** \brief  Unmount, leaving stored data intact. */
  bool end();

  /** \brief  Unmount, erase and re-mount. All data is lost. */
  bool format();

  /** \brief  Open for reading. */
  File open(const char* path);
  /** \brief  Open with an explicit mode; FILE_O_WRITE truncates or creates. */
  File open(const char* path, uint8_t mode);

  bool exists(const char* path);
  bool remove(const char* path);
  bool mkdir(const char* path);

  bool isMounted() const { return _mounted; }

private:
  bool _mounted = false;
};

extern LittleFileSystem InternalFS;

} // namespace mc_zephyr_fs
