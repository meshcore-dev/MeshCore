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
long          random(long max);
long          random(long min, long max);

#ifdef __cplusplus
}
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

#ifdef __cplusplus
template <typename T> static inline T mc_min(T a, T b) { return a < b ? a : b; }
template <typename T> static inline T mc_max(T a, T b) { return a > b ? a : b; }
#ifndef min
#define min(a, b) mc_min(a, b)
#endif
#ifndef max
#define max(a, b) mc_max(a, b)
#endif
#endif
