#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

class MockStream : public Stream {
public:
  uint8_t *buffer;
  size_t pos;
  MockStream(uint8_t *b)
  :buffer(b),pos(0)
  {
    buffer[0] = 0;
  }

  void clear() {
    pos = 0;
    buffer[0] = 0;
  }

  size_t write(uint8_t c) {
    buffer[pos++] = c;
    buffer[pos] = 0;
    return 1;
  }

  size_t write(const uint8_t *src, size_t size) override {
    memcpy(buffer, src, size);
    pos += size;
    return size;
  }

  MOCK_METHOD(int, available, (), (override));
  MOCK_METHOD(int, availableForWrite, (), (override));
  MOCK_METHOD(int, read, (), (override));
  MOCK_METHOD(int, peek, (), (override));
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

