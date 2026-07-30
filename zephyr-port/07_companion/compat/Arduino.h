/*
 * Minimal Arduino-compatibility shim for building MeshCore (and its Arduino-style
 * deps: rweather/Crypto, ArduinoHelpers) under Zephyr. Mapped to Zephyr APIs.
 * Lives on the include path so MeshCore's `#include <Arduino.h>` resolves here.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/random.h>

typedef uint8_t  byte;
typedef bool     boolean;

#ifndef HIGH
#define HIGH 1
#define LOW  0
#define INPUT  0
#define OUTPUT 1
#define INPUT_PULLUP 2
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef constrain
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif
#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif

/* Flash-string helpers are no-ops (everything is in RAM/RRAM here). */
#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
typedef const char *__FlashStringHelper;

#ifdef __cplusplus

#include "Print.h"
#include "Stream.h"

static inline uint32_t millis(void) { return (uint32_t)k_uptime_get(); }
static inline uint32_t micros(void) { return (uint32_t)k_ticks_to_us_floor64(k_uptime_ticks()); }
static inline void delay(uint32_t ms) { k_msleep((int32_t)ms); }
static inline void delayMicroseconds(uint32_t us) { k_busy_wait(us); }
static inline void yield(void) {}

/* newlib-nano / picolibc omit ltoa (MeshCore's TxtDataHelpers uses it). */
#ifndef MC_COMPAT_LTOA
#define MC_COMPAT_LTOA
static inline char *ltoa(long value, char *result, int base) {
	if (base < 2 || base > 36) { *result = '\0'; return result; }
	char *ptr = result, *ptr1 = result, tmp_char;
	long tmp_value;
	do {
		tmp_value = value;
		value /= base;
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + (tmp_value - value * base)];
	} while (value);
	if (tmp_value < 0) *ptr++ = '-';
	*ptr-- = '\0';
	while (ptr1 < ptr) { tmp_char = *ptr; *ptr-- = *ptr1; *ptr1++ = tmp_char; }
	return result;
}
#endif

static inline void randomSeed(unsigned long seed) { (void)seed; }  /* HW TRNG, seed ignored */
static inline long random(long howbig) {
	return howbig > 0 ? (long)(sys_rand32_get() % (uint32_t)howbig) : 0;
}
static inline long random(long howsmall, long howbig) {
	return howbig <= howsmall ? howsmall : howsmall + random(howbig - howsmall);
}

/* Serial -> printk shim. Inherits Print (print/println overloads); adds printf. */
class SerialShim : public Stream {
  public:
	void begin(unsigned long) {}
	void end() {}
	void flush() {}
	explicit operator bool() const { return true; }
	using Print::write;
	size_t write(uint8_t c) override { printk("%c", c); return 1; }
	__attribute__((format(printf, 2, 3)))
	int printf(const char *fmt, ...) {
		va_list ap; va_start(ap, fmt);
		vprintk(fmt, ap);
		va_end(ap);
		return 0;
	}
};
extern SerialShim Serial;

#endif /* __cplusplus */
