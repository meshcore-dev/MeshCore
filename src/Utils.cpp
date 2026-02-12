#include "Utils.h"
#include <AES.h>
#include <SHA256.h>
#include <ChaChaPoly.h>

#ifdef ARDUINO
  #include <Arduino.h>
#endif

namespace mesh {

uint32_t RNG::nextInt(uint32_t _min, uint32_t _max) {
  uint32_t num;
  random((uint8_t *) &num, sizeof(num));
  return (num % (_max - _min)) + _min;
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t* msg, int msg_len) {
  SHA256 sha;
  sha.update(msg, msg_len);
  sha.finalize(hash, hash_len);
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t* frag1, int frag1_len, const uint8_t* frag2, int frag2_len) {
  SHA256 sha;
  sha.update(frag1, frag1_len);
  sha.update(frag2, frag2_len);
  sha.finalize(hash, hash_len);
}

int Utils::decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  AES128 aes;
  uint8_t* dp = dest;
  const uint8_t* sp = src;

  aes.setKey(shared_secret, CIPHER_KEY_SIZE);
  while (sp - src < src_len) {
    aes.decryptBlock(dp, sp);
    dp += 16; sp += 16;
  }

  return sp - src;  // will always be multiple of 16
}

int Utils::encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  AES128 aes;
  uint8_t* dp = dest;

  aes.setKey(shared_secret, CIPHER_KEY_SIZE);
  while (src_len >= 16) {
    aes.encryptBlock(dp, src);
    dp += 16; src += 16; src_len -= 16;
  }
  if (src_len > 0) {  // remaining partial block
    uint8_t tmp[16];
    memset(tmp, 0, 16);
    memcpy(tmp, src, src_len);
    aes.encryptBlock(dp, tmp);
    dp += 16;
  }
  return dp - dest;  // will always be multiple of 16
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

/*
 * AEAD-4: ChaCha20-Poly1305 authenticated encryption with 4-byte tag.
 *
 * Wire format (replaces ECB's [HMAC:2][ciphertext:N*16]):
 *   [nonce:2] [ciphertext:M] [tag:4]       (M = exact plaintext length)
 *
 * Key derivation (per-message, eliminates nonce-reuse catastrophe):
 *   msg_key[32] = HMAC-SHA256(shared_secret[32], nonce_hi || nonce_lo || dest_hash || src_hash)
 *   Including hashes makes keys direction-dependent: Alice->Bob and Bob->Alice derive
 *   different keys even with the same nonce (for 255/256 peer pairs; the 1/256 where
 *   dest_hash == src_hash remains a residual risk inherent to 1-byte hashes).
 *
 * IV construction (12 bytes, from on-wire fields):
 *   iv[12] = { nonce_hi, nonce_lo, dest_hash, src_hash, 0, 0, 0, 0, 0, 0, 0, 0 }
 *
 * Associated data (authenticated but not encrypted):
 *   Peer msgs:  header || dest_hash || src_hash
 *   Anon reqs:  header || dest_hash
 *   Group msgs: header || channel_hash
 *
 * Nonce: 16-bit counter per peer, seeded from HW RNG on boot. With per-message
 * key derivation, even a nonce collision (across reboots) only leaks P1 XOR P2
 * for that message pair — no key recovery, no impact on other messages.
 *
 * Group channels: all members share the same key, so cross-sender nonce
 * collisions are possible (~300 msgs for 50% chance with random nonces).
 * Damage is bounded (message pair leak, no key recovery).
 */
int Utils::aeadEncrypt(const uint8_t* shared_secret,
                       uint8_t* dest,
                       const uint8_t* src, int src_len,
                       const uint8_t* assoc_data, int assoc_len,
                       uint16_t nonce_counter,
                       uint8_t dest_hash, uint8_t src_hash) {
  if (src_len <= 0) return 0;

  // Write nonce to output
  dest[0] = (uint8_t)(nonce_counter >> 8);
  dest[1] = (uint8_t)(nonce_counter & 0xFF);

  // Derive per-message key: HMAC-SHA256(shared_secret, nonce || dest_hash || src_hash)
  // Including hashes makes the key direction-dependent, preventing keystream reuse
  // when Alice->Bob and Bob->Alice use the same nonce (255/256 peer pairs).
  uint8_t msg_key[32];
  {
    uint8_t kdf_input[AEAD_NONCE_SIZE + 2] = { dest[0], dest[1], dest_hash, src_hash };
    SHA256 sha;
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(kdf_input, sizeof(kdf_input));
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, msg_key, 32);
  }

  // Build 12-byte IV from on-wire fields
  uint8_t iv[12];
  iv[0] = dest[0];  // nonce_hi
  iv[1] = dest[1];  // nonce_lo
  iv[2] = dest_hash;
  iv[3] = src_hash;
  memset(&iv[4], 0, 8);

  ChaChaPoly cipher;
  cipher.setKey(msg_key, 32);
  cipher.setIV(iv, 12);
  cipher.addAuthData(assoc_data, assoc_len);
  cipher.encrypt(dest + AEAD_NONCE_SIZE, src, src_len);
  cipher.computeTag(dest + AEAD_NONCE_SIZE + src_len, AEAD_TAG_SIZE);
  cipher.clear();
  memset(msg_key, 0, 32);

  return AEAD_NONCE_SIZE + src_len + AEAD_TAG_SIZE;
}

int Utils::aeadDecrypt(const uint8_t* shared_secret,
                       uint8_t* dest,
                       const uint8_t* src, int src_len,
                       const uint8_t* assoc_data, int assoc_len,
                       uint8_t dest_hash, uint8_t src_hash) {
  // Minimum: nonce(2) + at least 1 byte ciphertext + tag(4)
  if (src_len < AEAD_NONCE_SIZE + 1 + AEAD_TAG_SIZE) return 0;

  int ct_len = src_len - AEAD_NONCE_SIZE - AEAD_TAG_SIZE;

  // Derive per-message key: HMAC-SHA256(shared_secret, nonce || dest_hash || src_hash)
  uint8_t msg_key[32];
  {
    uint8_t kdf_input[AEAD_NONCE_SIZE + 2] = { src[0], src[1], dest_hash, src_hash };
    SHA256 sha;
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(kdf_input, sizeof(kdf_input));
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, msg_key, 32);
  }

  // Build 12-byte IV from on-wire fields
  uint8_t iv[12];
  iv[0] = src[0];  // nonce_hi
  iv[1] = src[1];  // nonce_lo
  iv[2] = dest_hash;
  iv[3] = src_hash;
  memset(&iv[4], 0, 8);

  ChaChaPoly cipher;
  cipher.setKey(msg_key, 32);
  cipher.setIV(iv, 12);
  cipher.addAuthData(assoc_data, assoc_len);
  cipher.decrypt(dest, src + AEAD_NONCE_SIZE, ct_len);

  bool valid = cipher.checkTag(src + AEAD_NONCE_SIZE + ct_len, AEAD_TAG_SIZE);
  cipher.clear();
  memset(msg_key, 0, 32);
  if (!valid) memset(dest, 0, ct_len);

  return valid ? ct_len : 0;
}

static const char hex_chars[] = "0123456789ABCDEF";

void Utils::toHex(char* dest, const uint8_t* src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    *dest++ = hex_chars[b >> 4];
    *dest++ = hex_chars[b & 0x0F];
    len--;
  }
  *dest = 0;
}

void Utils::printHex(Stream& s, const uint8_t* src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    s.print(hex_chars[b >> 4]);
    s.print(hex_chars[b & 0x0F]);
    len--;
  }
}

static uint8_t hexVal(char c) {
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= '0' && c <= '9') return c - '0';
  return 0;
}

bool Utils::isHexChar(char c) {
  return c == '0' || hexVal(c) > 0;
}

bool Utils::fromHex(uint8_t* dest, int dest_size, const char *src_hex) {
  int len = strlen(src_hex);
  if (len != dest_size*2) return false;  // incorrect length

  uint8_t* dp = dest;
  while (dp - dest < dest_size) {
    char ch = *src_hex++;
    char cl = *src_hex++;
    *dp++ = (hexVal(ch) << 4) | hexVal(cl);
  }
  return true;
}

int Utils::parseTextParts(char* text, const char* parts[], int max_num, char separator) {
  int num = 0;
  char* sp = text;
  while (*sp && num < max_num) {
    parts[num++] = sp;
    while (*sp && *sp != separator) sp++;
    if (*sp) {
       *sp++ = 0;  // replace the seperator with a null, and skip past it
    }
  }
  // if we hit the maximum parts, make sure LAST entry does NOT have separator 
  while (*sp && *sp != separator) sp++;
  if (*sp) {
    *sp = 0;  // replace the separator with null
  }
  return num;
}

}