#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <ed_25519.h>
#ifdef __cplusplus
}
#endif

#include <stdint.h>

class Ed25519 {
public:
  static bool verify(const uint8_t* sig, const uint8_t* pub_key, const uint8_t* message, int msg_len) {
    return ed25519_verify(sig, message, msg_len, pub_key) != 0;
  }
};
