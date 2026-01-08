#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <stdint.h>
#include <Stream.h>

MATCHER_P2(MemcmpAs, cb, len, "") {
  bool result = true;
  for (size_t i = 0; i < len; i++) {
    if (cb[i] != arg[i]) {
      *result_listener << "element #" << std::dec << i
        << " differ: " << " 0x" << std::hex << static_cast<unsigned int>(arg[i])
        << " vs "      << " 0x" << std::hex << static_cast<unsigned int>(cb[i])
        << "\n";
      result = false;
    } else {
      *result_listener << "element #" << std::dec << i
        << " matches: " << " 0x" << std::hex << static_cast<unsigned int>(arg[i]) << "\n";
    }
  }
  return result;
}

class MockStream : public Stream {
public:
  uint8_t *buffer;
  size_t pos, cap;
  bool own_buffer;

  //  internal buffer; can expand
  MockStream()
  :pos(0),cap(0),own_buffer(true)
  {
    buffer = {0};
  }

  // external buffer; assumed infinite, can't expand
  MockStream(uint8_t *b)
  :buffer(b),pos(0),cap(SIZE_MAX),own_buffer(false)
  {
    buffer[0] = 0;
  }

  // external buffer, known size, can't expand
  MockStream(uint8_t *b, size_t sz)
  :buffer(b),pos(0),cap(sz),own_buffer(false)
  {
    if (cap>0) buffer[0] = 0;
  }

  virtual ~MockStream() {
    if (own_buffer && buffer != nullptr) {
      free(buffer);
    }
  }

  void clear() {
    pos = 0;
    if (cap>0) {
      buffer[0] = 0;
    }
  }

  size_t write(uint8_t c) {
    if (!expand(pos+1)) return 0;
    buffer[pos++] = c;
    if (cap>pos) buffer[pos] = 0;
    return 1;
  }

  size_t write(const uint8_t *src, size_t len) {
    if (!expand(pos+len)) return 0;
    memcpy(buffer+pos, src, len);
    pos += len;
    if (cap>pos) buffer[pos] = 0;
    return len;
  }

  MOCK_METHOD(int, available, (), (override));
  MOCK_METHOD(int, availableForWrite, (), (override));
  MOCK_METHOD(int, read, (), (override));
  MOCK_METHOD(int, peek, (), (override));

private:
  bool expand(size_t newsize) {
    if (newsize > cap) {
      if (!own_buffer) return false;
      newsize = (newsize+0x1f) & (~0x1f);  // round up to next 32
      uint8_t *exp = (uint8_t *)realloc(buffer, newsize);
      if (exp == nullptr) {
        return false;
      }
      buffer = exp;
      cap = newsize;
    }
    return true;
  }
};

class ConstantValueStream : public Stream {
public:
  const uint8_t *buffer;
  size_t pos, len;

  ConstantValueStream(const uint8_t *b, size_t l)
  :buffer(b),pos(0),len(l)
  {}

  int available() {
    return (int)(len - pos);
  }
  MOCK_METHOD(size_t, write, (uint8_t c), (override));
  MOCK_METHOD(size_t, write, (const uint8_t *buffer, size_t size), (override));
  MOCK_METHOD(int, availableForWrite, (), (override));
  int read() {
    if (pos >= len) {
      return 0;
    }
    return (int)buffer[pos++];
  }
  MOCK_METHOD(int, peek, (), (override));
};

