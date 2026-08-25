#include "Utils.h"
#include <AES.h>
#include <SHA256.h>

#ifdef USE_CC310_HW_CRYPTO
#include <Adafruit_nRFCrypto.h>
#include "nrf_cc310/include/crys_hash.h"
#include "nrf_cc310/include/crys_hmac.h"
#include "nrf_cc310/include/ssi_aes.h"
#endif

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
#ifdef USE_CC310_HW_CRYPTO
  static CRYS_HASH_Result_t result;
  CRYS_HASH(CRYS_HASH_SHA256_mode, (uint8_t*)msg, (size_t)msg_len, result);
  memcpy(hash, result, hash_len);
#else
  SHA256 sha;
  sha.update(msg, msg_len);
  sha.finalize(hash, hash_len);
#endif
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t* frag1, int frag1_len, const uint8_t* frag2, int frag2_len) {
#ifdef USE_CC310_HW_CRYPTO
  static CRYS_HASHUserContext_t ctx;
  static CRYS_HASH_Result_t result;
  CRYS_HASH_Init(&ctx, CRYS_HASH_SHA256_mode);
  CRYS_HASH_Update(&ctx, (uint8_t*)frag1, (size_t)frag1_len);
  CRYS_HASH_Update(&ctx, (uint8_t*)frag2, (size_t)frag2_len);
  CRYS_HASH_Finish(&ctx, result);
  memcpy(hash, result, hash_len);
#else
  SHA256 sha;
  sha.update(frag1, frag1_len);
  sha.update(frag2, frag2_len);
  sha.finalize(hash, hash_len);
#endif
}

int Utils::decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
#ifdef USE_CC310_HW_CRYPTO
  static SaSiAesUserContext_t ctx;
  SaSiAesUserKeyData_t keyData = { (uint8_t*)shared_secret, CIPHER_KEY_SIZE };
  uint8_t* dp = dest;
  const uint8_t* sp = src;
  size_t dummy_out = 0;

  SaSi_AesInit(&ctx, SASI_AES_DECRYPT, SASI_AES_MODE_ECB, SASI_AES_PADDING_NONE);
  SaSi_AesSetKey(&ctx, SASI_AES_USER_KEY, &keyData, sizeof(keyData));
  while (sp - src < src_len) {
    SaSi_AesBlock(&ctx, (uint8_t*)sp, 16, dp);
    dp += 16; sp += 16;
  }
  SaSi_AesFinish(&ctx, 0, NULL, 0, NULL, &dummy_out);
  SaSi_AesFree(&ctx);
  return sp - src;
#else
  AES128 aes;
  uint8_t* dp = dest;
  const uint8_t* sp = src;

  aes.setKey(shared_secret, CIPHER_KEY_SIZE);
  while (sp - src < src_len) {
    aes.decryptBlock(dp, sp);
    dp += 16; sp += 16;
  }

  return sp - src;  // will always be multiple of 16
#endif
}

int Utils::encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
#ifdef USE_CC310_HW_CRYPTO
  static SaSiAesUserContext_t ctx;
  SaSiAesUserKeyData_t keyData = { (uint8_t*)shared_secret, CIPHER_KEY_SIZE };
  uint8_t* dp = dest;
  size_t dummy_out = 0;

  SaSi_AesInit(&ctx, SASI_AES_ENCRYPT, SASI_AES_MODE_ECB, SASI_AES_PADDING_NONE);
  SaSi_AesSetKey(&ctx, SASI_AES_USER_KEY, &keyData, sizeof(keyData));
  while (src_len >= 16) {
    SaSi_AesBlock(&ctx, (uint8_t*)src, 16, dp);
    dp += 16; src += 16; src_len -= 16;
  }
  if (src_len > 0) {  // remaining partial block — zero-pad to 16 bytes
    uint8_t tmp[16] = {};
    memcpy(tmp, src, src_len);
    SaSi_AesBlock(&ctx, tmp, 16, dp);
    dp += 16;
  }
  SaSi_AesFinish(&ctx, 0, NULL, 0, NULL, &dummy_out);
  SaSi_AesFree(&ctx);
  return dp - dest;
#else
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
#endif
}

int Utils::encryptThenMAC(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  int enc_len = encrypt(shared_secret, dest + CIPHER_MAC_SIZE, src, src_len);

#ifdef USE_CC310_HW_CRYPTO
  static CRYS_HMACUserContext_t hmac_ctx;
  static CRYS_HASH_Result_t hmac_result;
  CRYS_HMAC_Init(&hmac_ctx, CRYS_HASH_SHA256_mode, (uint8_t*)shared_secret, PUB_KEY_SIZE);
  CRYS_HMAC_Update(&hmac_ctx, dest + CIPHER_MAC_SIZE, enc_len);
  CRYS_HMAC_Finish(&hmac_ctx, hmac_result);
  memcpy(dest, hmac_result, CIPHER_MAC_SIZE);
#else
  SHA256 sha;
  sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
  sha.update(dest + CIPHER_MAC_SIZE, enc_len);
  sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, dest, CIPHER_MAC_SIZE);
#endif

  return CIPHER_MAC_SIZE + enc_len;
}

int Utils::MACThenDecrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  if (src_len <= CIPHER_MAC_SIZE) return 0;  // invalid src bytes

  uint8_t hmac[CIPHER_MAC_SIZE];
#ifdef USE_CC310_HW_CRYPTO
  {
    static CRYS_HMACUserContext_t hmac_ctx;
    static CRYS_HASH_Result_t hmac_result;
    CRYS_HMAC_Init(&hmac_ctx, CRYS_HASH_SHA256_mode, (uint8_t*)shared_secret, PUB_KEY_SIZE);
    CRYS_HMAC_Update(&hmac_ctx, (uint8_t*)(src + CIPHER_MAC_SIZE), src_len - CIPHER_MAC_SIZE);
    CRYS_HMAC_Finish(&hmac_ctx, hmac_result);
    memcpy(hmac, hmac_result, CIPHER_MAC_SIZE);
  }
#else
  {
    SHA256 sha;
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, hmac, CIPHER_MAC_SIZE);
  }
#endif
  if (memcmp(hmac, src, CIPHER_MAC_SIZE) == 0) {
    return decrypt(shared_secret, dest, src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
  }
  return 0; // invalid HMAC
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
