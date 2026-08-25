#pragma once

// Opt-in OTA tracing over Serial: build with -D OTA_DEBUG for EndF scan, self-serve, fetch
// (ADV/REQ/block/page-flush) during bring-up. Compiles to nothing otherwise, and on the native host
// (no Arduino), so it never touches a non-debug or test build.
#if defined(OTA_DEBUG) && defined(ARDUINO)
  #include <Arduino.h>
  #define OTA_DBG(...) do { Serial.print("\r"); Serial.printf(__VA_ARGS__); Serial.print("\r\n"); } while (0)
  #define OTA_DBG_MS(...) do { Serial.printf("\r[%lu] ", (unsigned long)millis()); Serial.printf(__VA_ARGS__); Serial.print("\r\n"); } while (0)
#else
  #define OTA_DBG(...) do {} while (0)
  #define OTA_DBG_MS(...) do {} while (0)
#endif
