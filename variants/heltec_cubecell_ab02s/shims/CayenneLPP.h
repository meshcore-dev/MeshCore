#pragma once
#include <stdint.h>
#include <string.h>

// Minimal subset used by MyMesh (voltage = type 116, uint16 0.01 V; temperature = type 103, int16 0.1 C;
// big-endian, as in the ElectronicCats CayenneLPP library). Not verified against upstream byte-for-byte.
class CayenneLPP {
  uint8_t _buf[64];
  uint8_t _cursor;
public:
  explicit CayenneLPP(uint8_t) : _cursor(0) {}
  void reset() { _cursor = 0; }
  uint8_t getSize() const { return _cursor; }
  uint8_t* getBuffer() { return _buf; }
  uint8_t addVoltage(uint8_t channel, float volts) {
    if (_cursor + 4 > sizeof(_buf)) return 0;
    uint16_t v = (uint16_t)(volts * 100.0f + 0.5f);
    _buf[_cursor++] = channel; _buf[_cursor++] = 116;
    _buf[_cursor++] = v >> 8; _buf[_cursor++] = v & 0xFF;
    return _cursor;
  }
  uint8_t addTemperature(uint8_t channel, float celsius) {
    if (_cursor + 4 > sizeof(_buf)) return 0;
    int16_t v = (int16_t)(celsius * 10.0f);
    _buf[_cursor++] = channel; _buf[_cursor++] = 103;
    _buf[_cursor++] = (uint16_t)v >> 8; _buf[_cursor++] = (uint16_t)v & 0xFF;
    return _cursor;
  }
};
