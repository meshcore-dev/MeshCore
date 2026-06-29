#pragma once

// Opt-in OTA tracing over Serial: build with -D OTA_DEBUG to watch the fetch (ADV/REQ/block/page-flush)
// during bring-up. Compiles to nothing otherwise, and on the native host (no Arduino), so it never
// touches a non-debug or test build.
#if defined(OTA_DEBUG) && defined(ARDUINO)
  #include <Arduino.h>
  #define OTA_DBG(...) do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define OTA_DBG(...) do {} while (0)
#endif
