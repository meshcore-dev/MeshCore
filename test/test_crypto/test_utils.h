#pragma once

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <Arduino.h>

inline uint32_t get_time_ms() { return millis(); }

/**
 * \brief Benchmark helper: measure elapsed time in milliseconds
 */
class BenchmarkTimer {
private:
  uint32_t start_time;
  uint32_t end_time;

public:
  BenchmarkTimer() : start_time(0), end_time(0) {}

  void start() {
    start_time = get_time_ms();
  }

  void stop() {
    end_time = get_time_ms();
  }

  uint32_t elapsed_ms() const {
    return (end_time >= start_time) ? (end_time - start_time) : 0;
  }
};

/**
 * \brief Fill buffer with deterministic pseudo-random data for testing
 * Uses a simple LCMG for reproducibility across platforms
 */
inline void fill_test_data(uint8_t* buf, size_t len, uint32_t seed) {
  uint32_t state = seed;
  for (size_t i = 0; i < len; i++) {
    state = (state * 1103515245 + 12345) & 0x7fffffff;
    buf[i] = (uint8_t)(state >> 16);
  }
}

/**
 * \brief Check if two buffers are equal
 */
inline bool buffers_equal(const uint8_t* a, const uint8_t* b, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

// Debug output verbosity levels
enum DebugLevel {
  DEBUG_SILENT = 0,
  DEBUG_ERROR = 1,
  DEBUG_INFO = 2,
  DEBUG_VERBOSE = 3,
  DEBUG_TRACE = 4
};

// Global debug level (can be overridden)
#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL DEBUG_VERBOSE
#endif

/**
 * \brief Debug output that goes to Serial
 */
inline void debug_print(int level, const char* format, ...) {
  if (level > DEBUG_LEVEL) return;

  va_list args;
  va_start(args, format);
  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), format, args);
  Serial.print(buffer);
  va_end(args);
}

/**
 * \brief Print hex dump of buffer (with length guard for embedded systems)
 */
inline void print_hex(const uint8_t* buf, size_t len, size_t max_print = 32) {
  size_t to_print = (len > max_print) ? max_print : len;
  debug_print(DEBUG_TRACE, "  ");
  for (size_t i = 0; i < to_print; i++) {
    debug_print(DEBUG_TRACE, "%02x", buf[i]);
    if ((i + 1) % 16 == 0) debug_print(DEBUG_TRACE, "\n  ");
  }
  if (len > max_print) debug_print(DEBUG_TRACE, "... (truncated, total %zu bytes)\n", len);
  debug_print(DEBUG_TRACE, "\n");
}

// Convenience macros for different debug levels
#define DBG_ERROR(fmt, ...) debug_print(DEBUG_ERROR, fmt "\n", ##__VA_ARGS__)
#define DBG_INFO(fmt, ...) debug_print(DEBUG_INFO, fmt "\n", ##__VA_ARGS__)
#define DBG_VERBOSE(fmt, ...) debug_print(DEBUG_VERBOSE, fmt "\n", ##__VA_ARGS__)
#define DBG_TRACE(fmt, ...) debug_print(DEBUG_TRACE, fmt "\n", ##__VA_ARGS__)