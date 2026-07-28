#include "GroupChannel.h"
#include "Utils.h"

#define DEFAULT_PUBLIC_CHANNEL_SECRET_HEX "8b3387e9c5cdea6ac9e5edbaa115cd72"

namespace mesh {

bool GroupChannel::deriveHash(int secret_len) {
  if (secret_len != 16 && secret_len != 32) return false;

  Utils::sha256(hash, PATH_HASH_SIZE, secret, secret_len);
  return true;
}

void GroupChannel::deriveHash() {
  static uint8_t zeroes[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  if (memcmp(&secret[16], zeroes, 16) == 0) {
    deriveHash(16);
  } else {
    deriveHash(32);
  }
}

bool GroupChannel::derivePublicSecret(const char* name, uint8_t* dest_secret) {
  if (name == NULL || dest_secret == NULL) return false;

  memset(dest_secret, 0, PUB_KEY_SIZE);

  if (strcmp(name, "Public") == 0) {
    return Utils::fromHex(dest_secret, 16, DEFAULT_PUBLIC_CHANNEL_SECRET_HEX);
  }

  Utils::sha256(dest_secret, 16, (const uint8_t*)name, strlen(name));
  return true;
}

}
