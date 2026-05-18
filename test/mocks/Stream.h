#pragma once

// Mock Stream class for native testing
// Provides minimal interface needed by Utils.h

class Stream {
public:
    virtual void print(char c) {}
    virtual void print(const char* str) {}
};
