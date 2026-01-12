#pragma once

#include <stdint.h>

// Minimal CayenneLPP stub to satisfy builds that do not require telemetry packing.
class CayenneLPP {
 public:
  explicit CayenneLPP(uint8_t /*size*/ = 0) {}
  ~CayenneLPP() = default;

  void reset() {}
  uint8_t getSize() const { return 0; }
  uint8_t* getBuffer() { return nullptr; }
  uint8_t copy(uint8_t* /*dst*/) { return 0; }
  uint8_t getError() { return 0; }

  template <typename... Args>
  bool add(Args&&... /*args*/) {
    return false;
  }

  template <typename... Args>
  uint8_t decode(Args&&... /*args*/) {
    return 0;
  }
};
