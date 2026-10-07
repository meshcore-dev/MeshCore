// Force-included for nRF54 builds (see nrf54_base in platformio.ini).
// Adds a few things the nRF52 core has but lolren's nRF54 core is missing.
#pragma once

#include <stdlib.h>

static inline char* ltoa(long value, char* str, int base) {
  return itoa((int)value, str, base);  // long is int-sized on this target
}

#ifdef __cplusplus
#include <type_traits>
#include <WString.h>

// the core only defines String + String, so String + uint8_t (used by CayenneLPP) is ambiguous.
// Only uint8_t is matched, so other mixes still fail to compile rather than being truncated.
template <typename T, typename std::enable_if<std::is_same<T, unsigned char>::value, int>::type = 0>
inline String operator+(const String& s, T n) {
  return s + String((unsigned)n);
}
#endif
