//
// Zephyr-backed implementation of the MeshCore Arduino compatibility shim.
//
#include <Arduino.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>

// --- Timing ---------------------------------------------------------------
extern "C" unsigned long millis(void) {
  return (unsigned long)k_uptime_get();
}

extern "C" unsigned long micros(void) {
  return (unsigned long)(k_ticks_to_us_floor64(k_uptime_ticks()));
}

extern "C" void delay(unsigned long ms) {
  k_msleep((int32_t)ms);
}

extern "C" void delayMicroseconds(unsigned int us) {
  k_busy_wait(us);
}

// --- Randomness -----------------------------------------------------------
extern "C" void randomSeed(unsigned long /*seed*/) {
  // No-op: entropy is provided by the Zephyr RNG.
}

extern "C" long random(long max) {
  if (max <= 0) return 0;
  uint32_t v;
  sys_rand_get(&v, sizeof(v));
  return (long)(v % (uint32_t)max);
}

extern "C" long random(long min, long max) {
  if (max <= min) return min;
  return min + random(max - min);
}

// --- Serial ---------------------------------------------------------------
// Routes byte output to Zephyr's console via printk. Buffered per-byte writes
// are fine for the low-volume debug/logging paths the core uses.
size_t ZephyrSerial::write(uint8_t b) {
  printk("%c", (char)b);
  return 1;
}

ZephyrSerial Serial;
