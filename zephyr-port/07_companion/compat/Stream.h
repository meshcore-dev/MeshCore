/* Minimal Arduino Stream base for the Zephyr compat layer. */
#pragma once
#include "Print.h"

class Stream : public Print {
  public:
	virtual int available() { return 0; }
	virtual int read() { return -1; }
	virtual int peek() { return -1; }
	virtual void flush() {}
	virtual size_t readBytes(uint8_t *buf, size_t len) {
		size_t n = 0;
		while (n < len) { int c = read(); if (c < 0) break; buf[n++] = (uint8_t)c; }
		return n;
	}
};
