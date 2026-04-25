#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Mock Arduino compatibility header for native testing.
// Provides the small libc-backed helpers needed by Arduino-oriented code.
inline char* ltoa(long value, char* buffer, int base) {
  if (base == 10) {
    snprintf(buffer, 32, "%ld", value);
  } else {
    buffer[0] = 0;
  }
  return buffer;
}
