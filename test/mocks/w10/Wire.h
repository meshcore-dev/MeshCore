#pragma once

#include <array>
#include <vector>
#include <cstdint>

// Register-backed I2C device with injectable address, write and short-read failures
class TwoWire {
  std::vector<uint8_t> tx;
  uint8_t cursor = 0;
public:
  std::array<uint8_t, 256> registers{};
  bool nack = false;
  bool fail_write = false;
  bool short_read = false;
  int fail_write_register = -1;
  unsigned writes = 0;

  void beginTransmission(uint8_t address) { tx.clear(); }
  size_t write(uint8_t value) { tx.push_back(value); return 1; }
  size_t write(const uint8_t* data, size_t len) {
    tx.insert(tx.end(), data, data + len);
    return len;
  }
  uint8_t endTransmission() {
    if (nack || (tx.size() > 1 && (fail_write || tx[0] == fail_write_register))) return 4;
    cursor = tx[0];
    if (tx.size() > 1) {
      writes++;
      for (size_t i = 1; i < tx.size(); i++) registers[cursor++] = tx[i];
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t address, uint8_t count) { return short_read ? 0 : count; }
  int read() { return registers[cursor++]; }
};
