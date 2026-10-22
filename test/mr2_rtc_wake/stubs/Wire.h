#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct FakeWire {
  std::array<uint8_t, 256> registers{};
  std::vector<uint8_t> tx, rx, readRegisters;
  uint8_t pointer = 0;
  unsigned transfers = 0, queuedBytes = 0, dataWrites = 0, reads = 0, starts = 0;
  unsigned nackAt = 0, queueFailureAt = 0, dropWriteAt = 0, badReadAt = 0;
  unsigned corruptReadAt = 0;
  uint8_t readReported = 0, readDelivered = 0;
  bool absent = false, reservedReadBits = false;

  void beginTransmission(uint8_t) { tx.clear(); }
  size_t write(uint8_t value) {
    if (++queuedBytes == queueFailureAt) return 0;
    tx.push_back(value);
    return 1;
  }
  uint8_t endTransmission(bool = true) {
    ++transfers;
    if (absent || transfers == nackAt) return 2;
    if (tx.empty()) return 0;
    pointer = tx[0];
    if (tx.size() > 1 && ++dataWrites == dropWriteAt) return 0;
    for (size_t i = 1; i < tx.size(); ++i) {
      if (pointer == 0x0F && !(registers[pointer] & 0x04) && (tx[i] & 0x04)) ++starts;
      // RV-3028 status flags clear on zero writes; EEbusy is read-only.
      if (pointer == 0x0E) registers[pointer] &= tx[i] | 0x80;
      else registers[pointer] = tx[i];
      ++pointer;
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t, uint8_t count) {
    ++reads;
    readRegisters.push_back(pointer);
    rx.clear();
    if (absent) return 0;
    if (reads == badReadAt) {
      rx.assign(readDelivered, registers[pointer]);
      return readReported;
    }
    for (uint8_t i = 0; i < count; ++i) {
      const uint8_t address = pointer++;
      uint8_t value = registers[address];
      if (reads == corruptReadAt) value ^= address == 0x0E ? 0x08 : 0x01;
      if (reservedReadBits) {
        if (address == 0x0B) value |= 0xF0;
        if (address == 0x0F) value |= 0x40;
        if (address == 0x0E) value |= 0x80; // EEbusy is unrelated to TF.
      }
      rx.push_back(value);
    }
    return count;
  }
  int available() const { return static_cast<int>(rx.size()); }
  int read() {
    if (rx.empty()) return -1;
    const uint8_t value = rx.front();
    rx.erase(rx.begin());
    return value;
  }
};

extern FakeWire Wire;
