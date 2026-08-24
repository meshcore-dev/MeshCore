#pragma once
//
// Minimal Arduino compatibility header for building MeshCore on Zephyr RTOS.
//
// Provides only the free functions and the `Serial` object the MeshCore core
// relies on, implemented on top of the Zephyr kernel (see arduino_compat.cpp).
// This is a shim, not a full Arduino core: no digital/analog IO, no String, no
// SPI/Wire. Those belong to board/transport ports.

#include <stddef.h>
#include <stdint.h>
#include <Stream.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Timing ---------------------------------------------------------------
unsigned long millis(void);
unsigned long micros(void);
void          delay(unsigned long ms);
void          delayMicroseconds(unsigned int us);

// --- Randomness -----------------------------------------------------------
// randomSeed() is accepted for source-compat but ignored: the backing entropy
// comes from Zephyr's RNG, not a PRNG seed.
void          randomSeed(unsigned long seed);

#ifdef __cplusplus
}
#endif

// random() is deliberately NOT extern "C": C linkage cannot express overloading,
// and the name also has to coexist with POSIX `long random(void)` from
// <stdlib.h>. C++ overload resolution copes with that only while at most one of
// the declarations has C linkage, so these two stay C++.
#ifdef __cplusplus
long random(long max);
long random(long min, long max);
#endif

// --- Serial ---------------------------------------------------------------
// A printk-backed Stream. Declared here, defined in arduino_compat.cpp.
#ifdef __cplusplus
class ZephyrSerial : public Stream {
public:
  void begin(unsigned long /*baud*/) {}
  size_t write(uint8_t b) override;
  using Print::write;
};
extern ZephyrSerial Serial;
#endif

// --- Common Arduino helpers the tree occasionally expects -----------------
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif

// Function templates rather than Arduino's function-like macros: a `min`/`max`
// macro breaks every libstdc++ header that names std::min/std::max, and a Zephyr
// C++ build pulls those in. Skipped if something upstream already made them macros.
#ifdef __cplusplus
#if !defined(min)
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }
#endif
#if !defined(max)
template <typename T> inline T max(T a, T b) { return a > b ? a : b; }
#endif
#endif
