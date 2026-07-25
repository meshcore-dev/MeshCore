#pragma once

#include <cstddef>
#include <cstdint>

class Stream {
public:
    virtual ~Stream() = default;
    virtual size_t write(uint8_t) { return 1; }
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t total = 0;
        for (size_t i = 0; i < size; i++) {
            total += write(buffer[i]);
        }
        return total;
    }
    virtual int available() { return 0; }
    virtual int availableForWrite() { return 0; }
    virtual int read() { return -1; }
    virtual void flush() {}
    virtual void print(char) {}
    virtual void print(const char*) {}
};
