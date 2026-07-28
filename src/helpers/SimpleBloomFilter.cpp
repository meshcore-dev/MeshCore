#include <helpers/SimpleBloomFilter.h>

#include <SHA256.h>
#include <string.h>

#define BLOOM_GOLDEN_RATIO_32 0x9E3779B9UL

SimpleBloomFilter::SimpleBloomFilter() {
  _data = NULL;
  _byte_count = 0;
  _bit_count = 0;
  _hash_count = 0;
  _salt = NULL;
  _salt_len = 0;
  _set_bits = 0;
}

bool SimpleBloomFilter::begin(
  uint8_t* storage,
  uint16_t storage_len,
  uint32_t bit_count,
  uint8_t hash_count
) {
  if (storage == NULL) return false;
  if (storage_len == 0) return false;
  if (bit_count == 0) return false;
  if (hash_count == 0) return false;
  if (bit_count > ((uint32_t)storage_len * 8UL)) return false;

  _data = storage;
  _byte_count = storage_len;
  _bit_count = bit_count;
  _hash_count = hash_count;

  clear();
  return true;
}

void SimpleBloomFilter::setSalt(const uint8_t* salt, uint8_t salt_len) {
  _salt = salt;
  _salt_len = salt == NULL ? 0 : salt_len;
}

void SimpleBloomFilter::clear() {
  if (_data != NULL && _byte_count > 0) {
    memset(_data, 0, _byte_count);
  }

  _set_bits = 0;
}

bool SimpleBloomFilter::getBit(uint32_t bit) const {
  return (_data[bit >> 3] & (uint8_t)(1U << (bit & 7))) != 0;
}

void SimpleBloomFilter::setBit(uint32_t bit) {
  uint8_t mask = (uint8_t)(1U << (bit & 7));
  uint8_t* byte = &_data[bit >> 3];

  if ((*byte & mask) == 0) {
    *byte |= mask;
    _set_bits++;
  }
}

void SimpleBloomFilter::calculateDigest(
  uint8_t digest[],
  const uint8_t* data,
  size_t data_len
) const {
  SHA256 sha;

  if (_salt != NULL && _salt_len > 0) {
    sha.update(_salt, _salt_len);
  }

  sha.update(data, data_len);
  sha.finalize(digest, SHA256::HASH_SIZE);
}

uint32_t SimpleBloomFilter::calculateBit(
  const uint8_t digest[],
  uint8_t index
) const {
  uint32_t h1;
  uint32_t h2;

  memcpy(&h1, digest, sizeof(h1));
  memcpy(&h2, digest + sizeof(h1), sizeof(h2));

  if (h2 == 0) {
    h2 = BLOOM_GOLDEN_RATIO_32;
  }

  uint32_t i = (uint32_t)index;
  uint32_t mixed = h1 + (i * h2) + (i * i);

  return mixed % _bit_count;
}

bool SimpleBloomFilter::contains(const uint8_t* data, size_t data_len) const {
  if (_data == NULL) return false;
  if (_bit_count == 0) return false;
  if (_hash_count == 0) return false;
  if (data == NULL) return false;

  uint8_t digest[SHA256::HASH_SIZE];
  calculateDigest(digest, data, data_len);

  for (uint8_t i = 0; i < _hash_count; i++) {
    uint32_t bit = calculateBit(digest, i);

    if (!getBit(bit)) {
      return false;
    }
  }

  return true;
}

void SimpleBloomFilter::add(const uint8_t* data, size_t data_len) {
  if (_data == NULL) return;
  if (_bit_count == 0) return;
  if (_hash_count == 0) return;
  if (data == NULL) return;

  uint8_t digest[SHA256::HASH_SIZE];
  calculateDigest(digest, data, data_len);

  for (uint8_t i = 0; i < _hash_count; i++) {
    uint32_t bit = calculateBit(digest, i);
    setBit(bit);
  }
}

bool SimpleBloomFilter::isSaturated(uint8_t max_fill_percent) const {
  if (_bit_count == 0) return true;

  return (_set_bits * 100UL) >= (_bit_count * (uint32_t)max_fill_percent);
}