#include <Utils.h>
#include <string.h>
#include <Crypto.h>
#include <AES.h>
#include <SHA256.h>

// Native-only implementation of crypto functions that don't require Arduino
// This allows the native tests to link and run

// It's meant to be a carbon copy of the real implementation (padding, boundary checks, ...). It's obviously as broken.
// We should really refact Utils.cpp but I want to keep the diff of my PR as uncontroversial as possible.

namespace mesh {

int Utils::decrypt(void* aes, const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  uint8_t* dp = dest;
  const uint8_t* sp = src;

  ((AES128*)aes)->setKey(shared_secret, CIPHER_KEY_SIZE);
  while (sp - src < src_len) {
    ((AES128*)aes)->decryptBlock(dp, sp);
    dp += 16; sp += 16;
  }

  return sp - src;  // will always be multiple of 16
}

int Utils::decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  AES128 aes;
  return decrypt(&aes, shared_secret, dest, src, src_len);
}

int Utils::encrypt(void* aes, const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  uint8_t* dp = dest;

  ((AES128*)aes)->setKey(shared_secret, CIPHER_KEY_SIZE);
  while (src_len >= 16) {
    ((AES128*)aes)->encryptBlock(dp, src);
    dp += 16; src += 16; src_len -= 16;
  }
  if (src_len > 0) {  // remaining partial block
    uint8_t tmp[16];
    memset(tmp, 0, 16);
    memcpy(tmp, src, src_len);
    ((AES128*)aes)->encryptBlock(dp, tmp);
    dp += 16;
  }
  return dp - dest;  // will always be multiple of 16
}

int Utils::encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  AES128 aes;
  return encrypt(&aes, shared_secret, dest, src, src_len);
}

int Utils::encryptThenMAC(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  int enc_len = encrypt(shared_secret, dest + CIPHER_MAC_SIZE, src, src_len);

  SHA256 sha;
  sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
  sha.update(dest + CIPHER_MAC_SIZE, enc_len);
  sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, dest, CIPHER_MAC_SIZE);

  return CIPHER_MAC_SIZE + enc_len;
}

int Utils::MACThenDecrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  if (src_len <= CIPHER_MAC_SIZE) return 0;  // invalid src bytes

  uint8_t hmac[CIPHER_MAC_SIZE];
  {
    SHA256 sha;
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, hmac, CIPHER_MAC_SIZE);
  }
  if (memcmp(hmac, src, CIPHER_MAC_SIZE) == 0) {
    return decrypt(shared_secret, dest, src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
  }
  return 0; // invalid HMAC
}

}  // namespace mesh
