#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Mock Stream class for native testing
// Provides minimal interface needed by Utils.h

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print
{
public:
    virtual size_t write(uint8_t b) { return 1; }
    size_t write(const char *str)
    {
        if(str == NULL) {
            return 0;
        }
        return write((const uint8_t *) str, strlen(str));
    }
    virtual size_t write(const uint8_t *buffer, size_t size) { 
        size_t t = 0;
        for (int i = 0; i < size; i++) { t += write(buffer[i]); }
        return t;
    }
    size_t write(const char *buffer, size_t size)
    {
        return write((const uint8_t *) buffer, size);
    }

    virtual size_t print(unsigned char b, int r = DEC) { return 0; }
    virtual size_t print(int v, int r = DEC) { return 0; }
    virtual size_t print(unsigned int v, int r = DEC) { return 0; }
    virtual size_t print(long v, int r = DEC) { return 0; }
    virtual size_t print(unsigned long v, int r = DEC) { return 0; }
    virtual size_t print(long long v, int r = DEC) { return 0; }
    virtual size_t print(unsigned long long v, int r = DEC) { return 0; }
    virtual size_t print(double v, int p = 2) { return 0; }

    size_t print(char c) { return write(c); }
    size_t print(const char* str) { return write(str); }

    //size_t println(void)  { return 0; }
    
    virtual void flush() { /* Empty implementation for backward compatibility */ }    
};

class Stream: public Print
{
public:
    virtual ~Stream() = default;
    virtual int available() { return 0; }
    virtual int availableForWrite() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return 0; }

    virtual size_t readBytes(char *buffer, size_t length) { 
        size_t i = 0;
        while (i < length && available()) {
            buffer[i++] = read();
        }
        return i;
    }
    virtual size_t readBytes(uint8_t *buffer, size_t length)
    {
        return readBytes((char *) buffer, length);
    }
};