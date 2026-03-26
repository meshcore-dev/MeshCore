#pragma once

// Use combined check: ARDUINO must be defined AND we're not on NATIVE_PLATFORM
// This allows the Crypto library to compile on native builds that define ARDUINO for library compatibility
// while preventing inclusion of non-existent Arduino.h header on native platform
#if defined(ARDUINO) && !defined(NATIVE_PLATFORM)
  // For real Arduino builds, include Arduino framework's Stream.h
  #include <Arduino.h>
  // Arduino.h includes Stream.h from the framework
#else
  // Minimal Stream stub for native compilation (including native with ARDUINO compat)
  class Stream {
  public:
      virtual ~Stream() = default;
      virtual int available() { return 0; }
      virtual int read() { return -1; }
      virtual void write(uint8_t) {}
  };

  // Minimal Print stub
  class Print {
  public:
      virtual ~Print() = default;
      virtual size_t write(uint8_t) { return 0; }
  };
#endif
