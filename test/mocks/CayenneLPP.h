#pragma once

#include <cstddef>
#include <cstdint>

class CayenneLPP {
public:
  explicit CayenneLPP(size_t) {}
  const uint8_t* getBuffer() const { return nullptr; }
  uint16_t getSize() const { return 0; }
};
