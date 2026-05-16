#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class SimpleBloomFilter {
public:
  SimpleBloomFilter();

  bool begin(uint8_t* storage, uint16_t storage_len, uint32_t bit_count, uint8_t hash_count);

  void setSalt(const uint8_t* salt, uint8_t salt_len);
  void clear();

  bool contains(const uint8_t* data, size_t data_len) const;
  void add(const uint8_t* data, size_t data_len);

  bool isSaturated(uint8_t max_fill_percent) const;

private:
  uint8_t* _data;
  uint16_t _byte_count;
  uint32_t _bit_count;
  uint8_t _hash_count;

  const uint8_t* _salt;
  uint8_t _salt_len;

  uint32_t _set_bits;

  bool getBit(uint32_t bit) const;
  void setBit(uint32_t bit);

  void calculateDigest(uint8_t digest[], const uint8_t* data, size_t data_len) const;
  uint32_t calculateBit(const uint8_t digest[], uint8_t index) const;
};