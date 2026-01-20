#pragma once

#include <MeshCore.h>
#include <Stream.h>
#include <string.h>

namespace mesh {

class RNG {
public:
  virtual void random(uint8_t* dest, size_t sz) = 0;

  /**
   * \returns  random number between _min (inclusive) and _max (exclusive)
   */
  uint32_t nextInt(uint32_t _min, uint32_t _max);
};

class Utils {
public:
  /**
   * \brief  calculates the SHA256 hash of 'msg', storing in 'hash' and truncating the hash to 'hash_len' bytes.
  */
  static void sha256(uint8_t *hash, size_t hash_len, const uint8_t* msg, int msg_len);

  /**
   * \brief  calculates the SHA256 hash of two fragments, 'frag1' and 'frag2' (in that order), storing in 'hash' and truncating.
  */
  static void sha256(uint8_t *hash, size_t hash_len, const uint8_t* frag1, int frag1_len, const uint8_t* frag2, int frag2_len);

  /**
   * \brief  Encrypts the 'src' bytes using AES128 cipher, using 'shared_secret' as key, with key length fixed at CIPHER_KEY_SIZE.
   *         Final block is padded with zero bytes before encrypt. Result stored in 'dest'.
   * \returns  The length in bytes put into 'dest'. (rounded up to block size)
  */
  static int encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);

  /**
   * \brief  Decrypt the 'src' bytes using AES128 cipher, using 'shared_secret' as key, with key length fixed at CIPHER_KEY_SIZE.
   *         'src_len' should be multiple of block size, as returned by 'encrypt()'.
   * \returns  The length in bytes put into 'dest'. (dest may contain trailing zero bytes in final block)
  */
  static int decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);

  /**
   * \brief  encrypts bytes in src, then calculates MAC on ciphertext, inserting into leading bytes of 'dest'.
   * \returns  total length of bytes in 'dest' (MAC + ciphertext)
  */
  static int encryptThenMAC(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);

  /**
   * \brief  checks the MAC (in leading bytes of 'src'), then if valid, decrypts remaining bytes in src.
   * \returns  zero if MAC is invalid, otherwise the length of decrypted bytes in 'dest'
  */
  static int MACThenDecrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);

  /**
   * \brief  Encrypts with ChaCha20-Poly1305 AEAD cipher (v2 encryption).
   *         Layout: [nonce (12 bytes)] [ciphertext] [tag (12 bytes)]
   * \param  key  32-byte encryption key (shared secret)
   * \param  dest  destination buffer for encrypted output
   * \param  nonce  12-byte nonce (must be unique per message)
   * \param  plaintext  data to encrypt
   * \param  plaintext_len  length of plaintext
   * \param  aad  optional additional authenticated data (can be NULL)
   * \param  aad_len  length of AAD (0 if no AAD)
   * \returns  total length of encrypted output (nonce + ciphertext + tag)
  */
  static int encryptCHACHA(const uint8_t* key, uint8_t* dest, const uint8_t* nonce,
                           const uint8_t* plaintext, int plaintext_len,
                           const uint8_t* aad = nullptr, int aad_len = 0);

  /**
   * \brief  Decrypts with ChaCha20-Poly1305 AEAD cipher (v2 decryption).
   *         Expects layout: [nonce (12 bytes)] [ciphertext] [tag (12 bytes)]
   * \param  key  32-byte decryption key (shared secret)
   * \param  dest  destination buffer for decrypted plaintext
   * \param  src  encrypted data (nonce + ciphertext + tag)
   * \param  src_len  length of encrypted data
   * \param  aad  optional additional authenticated data (can be NULL)
   * \param  aad_len  length of AAD (0 if no AAD)
   * \returns  plaintext length on success, 0 on authentication failure
  */
  static int decryptCHACHA(const uint8_t* key, uint8_t* dest,
                           const uint8_t* src, int src_len,
                           const uint8_t* aad = nullptr, int aad_len = 0);

  /**
   * \brief  Get hardware random bytes from platform-specific TRNG.
   * \param  dest  destination buffer
   * \param  size  number of random bytes to generate
  */
  static void getHardwareRandom(uint8_t* dest, size_t size);

  /**
   * \brief  Get high-quality random bytes, preferring provided RNG.
   * \param  rng  optional RNG instance (e.g. RadioNoiseListener)
   * \param  dest  destination buffer
   * \param  size  number of random bytes to generate
  */
  static void getHighQualityRandom(RNG* rng, uint8_t* dest, size_t size);

  /**
   * \brief  Generate a secure nonce for ChaCha20-Poly1305.
   *         Uses hybrid format: [boot_id (4)] [counter (4)] [random (4)]
   *         This ensures uniqueness even if RNG has bias/periodicity.
   * \param  nonce  destination buffer (must be CHACHA_NONCE_SIZE bytes)
  */
  static void generateSecureNonce(uint8_t* nonce);

  /**
   * \brief  converts 'src' bytes with given length to Hex representation, and null terminates.
  */
  static void toHex(char* dest, const uint8_t* src, size_t len);

  /**
   * \brief  converts 'src_hex' hexadecimal string (should be null term) back to raw bytes, storing in 'dest'.
   * \param dest_size   must be exactly the expected size in bytes.
   * \returns  true if successful
  */
  static bool fromHex(uint8_t* dest, int dest_size, const char *src_hex);

  /**
   * \brief  Prints the hexadecimal representation of 'src' bytes of given length, to Stream 's'.
  */
  static void printHex(Stream& s, const uint8_t* src, size_t len);

  /**
   * \brief  parse 'text' into parts separated by 'separator' char.
   * \param  text  the text to parse (note is MODIFIED!)
   * \param  parts  destination array to store pointers to starts of parse parts
   * \param  max_num  max elements to store in 'parts' array
   * \param  separator  the separator character
   * \returns  the number of parts parsed (in 'parts')
   */
  static int parseTextParts(char* text, const char* parts[], int max_num, char separator=',');

  static bool isHexChar(char c);
};

}
