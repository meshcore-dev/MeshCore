#pragma once

#include <CustomLFS.h>
#include <InternalFileSystem.h>

// InternalFS and a CustomLFS on internal flash use the same flash cache, which
// has no lock of its own.  InternalFS uses it under its own lock, so take that
// lock around all operations here too.
class LockedLFS : public CustomLFS {
  lfs_config orig;

  static const lfs_config& base(const lfs_config* c) {
    return static_cast<LockedLFS*>(static_cast<CustomLFS*>(c->context))->orig;
  }

  static int read(const lfs_config* c, lfs_block_t b, lfs_off_t o, void* buf, lfs_size_t n) {
    InternalFS._lockFS();
    int r = base(c).read(c, b, o, buf, n);
    InternalFS._unlockFS();
    return r;
  }

  static int prog(const lfs_config* c, lfs_block_t b, lfs_off_t o, const void* buf, lfs_size_t n) {
    InternalFS._lockFS();
    int r = base(c).prog(c, b, o, buf, n);
    InternalFS._unlockFS();
    return r;
  }

  static int erase(const lfs_config* c, lfs_block_t b) {
    InternalFS._lockFS();
    int r = base(c).erase(c, b);
    InternalFS._unlockFS();
    return r;
  }

  static int sync(const lfs_config* c) {
    InternalFS._lockFS();
    int r = base(c).sync(c);
    InternalFS._unlockFS();
    return r;
  }

public:
  LockedLFS(uint32_t addr, uint32_t size, uint32_t block)
    : CustomLFS(addr, size, block), orig(_lfs_config) {
    _lfs_config.read = read;
    _lfs_config.prog = prog;
    _lfs_config.erase = erase;
    _lfs_config.sync = sync;
  }
};
