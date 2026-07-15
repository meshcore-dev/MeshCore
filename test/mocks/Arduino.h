#pragma once

// Minimal Arduino.h shim for native (host) unit tests.
// Only provides what the headers pulled in by RegionMap / TransportKeyStore /
// IdentityStore need to compile off-device. File / FILESYSTEM are inert stubs:
// the region-gating tests exercise in-memory logic and never touch the filesystem.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

#include "Stream.h"

// ---- Arduino string helpers not present in the host libc --------------------
static inline char* ltoa(long value, char* result, int base) {
  if (base < 2 || base > 36) { *result = '\0'; return result; }
  char* ptr = result;
  char* low = result;
  if (value < 0 && base == 10) { *ptr++ = '-'; low = ptr; }
  unsigned long uval = (value < 0 && base == 10) ? (unsigned long)(-value) : (unsigned long)value;
  do {
    int digit = (int)(uval % (unsigned long)base);
    *ptr++ = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    uval /= (unsigned long)base;
  } while (uval);
  *ptr-- = '\0';
  while (low < ptr) { char t = *low; *low++ = *ptr; *ptr-- = t; }
  return result;
}

// ---- filesystem stubs -------------------------------------------------------
class File {
public:
  operator bool() const { return false; }
  int read(uint8_t* buf, size_t len) { (void)buf; (void)len; return 0; }
  size_t write(const uint8_t* buf, size_t len) { (void)buf; (void)len; return 0; }
  void close() {}
};

class FsMock {
public:
  bool exists(const char* path) { (void)path; return false; }
  bool remove(const char* path) { (void)path; return false; }
  bool mkdir(const char* path) { (void)path; return false; }
  File open(const char* path) { (void)path; return File(); }
  File open(const char* path, const char* mode) { (void)path; (void)mode; return File(); }
  File open(const char* path, const char* mode, bool create) {
    (void)path; (void)mode; (void)create; return File();
  }
};

#ifndef FILESYSTEM
  #define FILESYSTEM FsMock
#endif
