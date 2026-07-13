#pragma once

// Mock Stream class for native testing
// Provides minimal interface needed by Utils.h

class Stream {
public:
    virtual void print(char c) {}
    virtual void print(const char* str) {}
    // Identity.cpp includes stream serialisation helpers, so native tests need
    // no-op Stream methods even though time-sync tests do not exercise I/O.
    virtual void println() {}
    virtual unsigned int readBytes(unsigned char* buffer, unsigned int length) {
        // Return zero bytes read to model an empty mock stream.
        (void)buffer;
        (void)length;
        return 0;
    }
    virtual unsigned int write(const unsigned char* buffer, unsigned int length) {
        // Report a successful write so Identity::writeTo can compile and link.
        (void)buffer;
        return length;
    }
};
