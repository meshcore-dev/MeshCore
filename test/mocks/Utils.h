#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

#define PUB_KEY_SIZE 32
#define PRV_KEY_SIZE 64
#define SIGNATURE_SIZE 64
#define CIPHER_MAC_SIZE 16

namespace mesh {

class RNG {
public:
  virtual ~RNG() = default;
  virtual void random(uint8_t* dest, size_t sz) = 0;
};

class Utils {
public:
  static void sha256(uint8_t* hash, size_t hash_len, const uint8_t*, int) {
    std::memset(hash, 0, hash_len);
  }

  static int encryptThenMAC(const uint8_t*, uint8_t* dest, const uint8_t* src, int src_len) {
    int out_len = src_len + CIPHER_MAC_SIZE;
    std::memset(dest, 0xAA, CIPHER_MAC_SIZE);
    std::memcpy(dest + CIPHER_MAC_SIZE, src, src_len);
    return out_len;
  }

  static int MACThenDecrypt(const uint8_t*, uint8_t* dest, const uint8_t* src, int src_len) {
    if (src_len < CIPHER_MAC_SIZE) {
      return 0;
    }
    int out_len = src_len - CIPHER_MAC_SIZE;
    std::memcpy(dest, src + CIPHER_MAC_SIZE, out_len);
    return out_len;
  }
};

}
