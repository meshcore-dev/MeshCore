#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock Stream class for native testing
class Stream {
public:
  virtual void print(char c) {}
  virtual void print(const char* str) {}
  virtual void println() {}
  virtual size_t write(const uint8_t* buf, size_t size) { return size; }
  virtual size_t write(uint8_t b) { return 1; }
  virtual size_t readBytes(uint8_t* buf, size_t size) { return 0; }
};
