#pragma once
//
// Minimal Arduino Print/Stream compatibility surface for MeshCore on Zephyr.
//
// The MeshCore *core* (src/*.cpp) only touches a tiny slice of the Arduino
// Stream API: print(char), print(const char*), printf(), println() and
// write(). This header provides exactly that, backed by whatever a concrete
// subclass implements in write(). See ports/zephyr/arduino_compat.cpp for the
// printk-backed `Serial` instance.
//
// This is intentionally NOT a full Arduino Stream implementation. Helpers that
// need String, Wire, SPI, or the full Stream read API still require porting.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

class Print {
public:
  virtual ~Print() {}

  // The one method subclasses must provide.
  virtual size_t write(uint8_t b) = 0;

  virtual size_t write(const uint8_t* buf, size_t len) {
    size_t n = 0;
    while (len--) n += write(*buf++);
    return n;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }

  size_t print(char c)          { return write((uint8_t)c); }
  size_t print(const char* s)   { return write(s); }
  size_t print(int v)           { return printf("%d", v); }
  size_t print(unsigned v)      { return printf("%u", v); }
  size_t print(long v)          { return printf("%ld", v); }
  size_t print(unsigned long v) { return printf("%lu", v); }

  size_t println()              { return write((uint8_t)'\n'); }
  size_t println(const char* s) { size_t n = write(s); return n + println(); }

  size_t printf(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    return write((const uint8_t*)buf, len);
  }
};

// Stream adds the input side of the API. The core never reads from it, so these
// are stubbed; override in a real UART/BLE transport port.
class Stream : public Print {
public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  virtual void flush() {}
};
