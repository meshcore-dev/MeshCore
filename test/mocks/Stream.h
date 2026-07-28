#pragma once

// Mock Stream class for native testing.
// Provides the Print/Stream surface used by Utils.cpp and RegionMap.cpp (BufStream).

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cstring>

class Stream {
public:
    virtual ~Stream() {}

    virtual size_t write(uint8_t c) { (void)c; return 1; }
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t written = 0;
        while (written < size) {
            if (!write(buffer[written])) break;
            written++;
        }
        return written;
    }

    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual void flush() {}

    size_t print(char c) { return write((uint8_t)c); }
    size_t print(const char* str) { return write((const uint8_t*)str, strlen(str)); }
    size_t printf(const char* fmt, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n < 0) return 0;
        if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
        return write((const uint8_t*)buf, (size_t)n);
    }
};
