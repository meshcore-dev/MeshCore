/* Minimal Arduino Print base for the Zephyr compat layer. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define DEC 10
#define HEX 16

class Print {
  public:
	__attribute__((format(printf, 2, 3)))
	int printf(const char *fmt, ...) {
		char b[160];
		va_list ap; va_start(ap, fmt);
		int n = vsnprintf(b, sizeof(b), fmt, ap);
		va_end(ap);
		if (n > (int)sizeof(b)) n = sizeof(b);
		return (int)write((const uint8_t *)b, n < 0 ? 0 : n);
	}
	virtual ~Print() {}
	virtual size_t write(uint8_t c) = 0;
	virtual size_t write(const uint8_t *buf, size_t size) {
		size_t n = 0;
		while (size--) { n += write(*buf++); }
		return n;
	}
	size_t write(const char *s) { return write((const uint8_t *)s, strlen_(s)); }

	size_t print(const char *s) { return write(s); }
	size_t print(char c) { return write((uint8_t)c); }
	size_t print(int v, int base = DEC) { return printNum((long)v, base); }
	size_t print(unsigned v, int base = DEC) { return printNum((unsigned long)v, base); }
	size_t print(long v, int base = DEC) { return printNum(v, base); }
	size_t print(unsigned long v, int base = DEC) { return printNum(v, base); }
	size_t print(double v) { char b[32]; int n = snprintf(b, sizeof(b), "%g", v); return write((const uint8_t *)b, n); }

	size_t println() { return write((uint8_t)'\n'); }
	size_t println(const char *s) { size_t n = print(s); return n + println(); }
	size_t println(int v, int base = DEC) { size_t n = print(v, base); return n + println(); }
	size_t println(unsigned long v, int base = DEC) { size_t n = print(v, base); return n + println(); }
	size_t println(double v) { size_t n = print(v); return n + println(); }

  private:
	static size_t strlen_(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
	size_t printNum(long v, int base) {
		char b[24];
		int n = (base == HEX) ? snprintf(b, sizeof(b), "%lx", (unsigned long)v)
				      : snprintf(b, sizeof(b), "%ld", v);
		return write((const uint8_t *)b, n);
	}
	size_t printNum(unsigned long v, int base) {
		char b[24];
		int n = (base == HEX) ? snprintf(b, sizeof(b), "%lx", v)
				      : snprintf(b, sizeof(b), "%lu", v);
		return write((const uint8_t *)b, n);
	}
};
