#pragma once

#include <MeshCore.h>
#include <string.h>

namespace mesh {

class GroupChannel {
public:
  uint8_t hash[PATH_HASH_SIZE];
  uint8_t secret[PUB_KEY_SIZE];

  void deriveHash();
  bool deriveHash(int secret_len);
  static bool derivePublicSecret(const char* name, uint8_t* dest_secret);
};
}
