#include "DebugLogger.h"

#ifndef CUSTOM_DEBUG_LOG
#include <Arduino.h>
#include <Utils.h>

namespace mesh {

class DefaultDebugLogger : public Logger {
public:
  DefaultDebugLogger() {}

  size_t write(uint8_t b) override { return Serial.write(b); }

  size_t write(const uint8_t *buffer, size_t size) override { return Serial.write(buffer, size); }

  size_t printf(const char *format, ...) override __attribute__((format(printf, 2, 3))) {
    va_list args;
    va_start(args, format);
    size_t written = vprintf(format, args);
    va_end(args);
    return written;
  }

  size_t print(char c) override { return Serial.print(c); }

  size_t print(const char str[]) override { return Serial.print(str); }

  size_t printlnf(const char *format, ...) override __attribute__((format(printf, 2, 3))) {
    va_list args;
    va_start(args, format);
    size_t written = vprintf(format, args);
    va_end(args);
    written += Serial.println();
    return written;
  }

  size_t println(void) override { return Serial.println(); }

  size_t println(char c) override { return Serial.println(c); }

  size_t println(const char str[]) override { return Serial.println(str); }

  void printHex(const uint8_t* src, size_t len) override {
    mesh::Utils::printHex(Serial, src, len);
  }

  void flush() override { Serial.flush(); }

private:
  size_t vprintf(const char* format, va_list arg) {
    char loc_buf[64];
    char *temp = loc_buf;
    va_list copy;
    va_copy(copy, arg);
    int len = vsnprintf(temp, sizeof(loc_buf), format, copy);
    va_end(copy);
    if (len < 0) {
      return 0;
    }
    if (len >= (int)sizeof(loc_buf)) { // comparation of same sign type for the compiler
      temp = (char *)malloc(len + 1);
      if (temp == NULL) {
        return 0;
      }
      len = vsnprintf(temp, len + 1, format, arg);
    }
    len = write((uint8_t *)temp, len);
    if (temp != loc_buf) {
      free(temp);
    }
    return len;
  }
};

DefaultDebugLogger defaultDebugLogger;
Logger& debugLog = defaultDebugLogger;

}  // namespace mesh

#endif
