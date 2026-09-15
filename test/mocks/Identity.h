#pragma once

#include <cstdint>
#include <cstring>
#include "Utils.h"

namespace mesh {

class Identity {
public:
  uint8_t pub_key[PUB_KEY_SIZE];

  Identity() {
    std::memset(pub_key, 0, sizeof(pub_key));
  }

  explicit Identity(const uint8_t* src) {
    std::memcpy(pub_key, src, PUB_KEY_SIZE);
  }

  bool verify(const uint8_t*, const uint8_t*, int) const {
    return true;
  }
};

class LocalIdentity : public Identity {
public:
  LocalIdentity() : Identity() {}

  void sign(uint8_t* sig, const uint8_t*, int) const {
    std::memset(sig, 0x5A, SIGNATURE_SIZE);
  }

  void calcSharedSecret(uint8_t* secret, const uint8_t*) const {
    std::memset(secret, 0x11, PUB_KEY_SIZE);
  }
};

}
