#pragma once

#include <stddef.h>
#include <stdint.h>

class Stream {
public:
    virtual size_t readBytes(uint8_t*, size_t) { return 0; }
    virtual size_t write(const uint8_t*, size_t len) { return len; }
    virtual void print(char) {}
    virtual void print(const char*) {}
    virtual void println() {}
};
