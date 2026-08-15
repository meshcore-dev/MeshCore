#pragma once
// Minimal Arduino Stream stub for host-side compilation.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

class Stream {
public:
  virtual size_t write(uint8_t b) { return fwrite(&b, 1, 1, stdout); }
  virtual size_t write(const uint8_t* buf, size_t len) { return fwrite(buf, 1, len, stdout); }
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }

  size_t print(const char* s) { return fputs(s, stdout); }
  size_t print(char c)        { return fputc(c, stdout); }
  size_t print(int n)         { return fprintf(stdout, "%d", n); }
  size_t println()            { return fputc('\n', stdout); }
  size_t println(const char* s) { size_t n = print(s); println(); return n; }

  size_t readBytes(uint8_t* buf, size_t len) { return 0; }
  size_t readBytes(char* buf, size_t len) { return 0; }
};
