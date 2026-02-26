#pragma once
// Minimal Arduino.h shim for native host builds.
// Provides standard-C equivalents for Arduino functions that MeshCore
// and its dependencies (rweather/Crypto) reference through Arduino.h.

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <chrono>

// --- Timing functions ---
// Used by Crypto library's RNG.cpp (millis, micros).
inline uint32_t millis() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - start).count();
}

inline uint32_t micros() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return duration_cast<microseconds>(steady_clock::now() - start).count();
}

// --- String conversion ---
// Arduino's ltoa() is a wrapper around standard C integer-to-string conversion.
#ifndef ltoa
inline char* ltoa(long value, char* str, int base) {
    if (base == 10) {
        sprintf(str, "%ld", value);
    } else if (base == 16) {
        sprintf(str, "%lx", value);
    } else if (base == 8) {
        sprintf(str, "%lo", value);
    }
    return str;
}
#endif
