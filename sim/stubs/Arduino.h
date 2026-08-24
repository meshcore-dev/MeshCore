#pragma once
// Minimal Arduino.h stub for host-side compilation of MeshCore core sources.
#include <stdint.h>
#include <stddef.h>
#include <cstddef>
using std::size_t;
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

typedef bool boolean;
typedef uint8_t byte;

#ifndef F
#define F(x) (x)
#endif

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}
