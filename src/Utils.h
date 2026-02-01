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
   * \brief  Ascon-128 AEAD encryption with per-packet key derivation.
   *         Layout: [counter (4 bytes)] [ciphertext] [tag (4 bytes)]
   *         Per-packet key = HMAC-SHA256(shared_secret, counter)[0:16]
   *
   * \param  shared_secret  32-byte shared secret from key exchange
   * \param  dest  destination buffer for encrypted output
   * \param  plaintext  data to encrypt
   * \param  plaintext_len  length of plaintext
   * \returns  total length of encrypted output (counter + ciphertext + tag)
  */
  static int encryptAscon(const uint8_t* shared_secret, uint8_t* dest,
                          const uint8_t* plaintext, int plaintext_len);

  /**
   * \brief  Ascon-128 AEAD decryption with per-packet key derivation.
   *         Expects layout: [counter (4 bytes)] [ciphertext] [tag (4 bytes)]
   *         Per-packet key = HMAC-SHA256(shared_secret, counter)[0:16]
   *
   * \param  shared_secret  32-byte shared secret from key exchange
   * \param  dest  destination buffer for decrypted plaintext
   * \param  src  encrypted data (counter + ciphertext + tag)
   * \param  src_len  length of encrypted data
   * \returns  plaintext length on success, 0 on authentication failure
  */
  static int decryptAscon(const uint8_t* shared_secret, uint8_t* dest,
                          const uint8_t* src, int src_len);

  /**
   * \brief  Unified decryption: tries Ascon first, falls back to legacy AES-ECB+HMAC.
   *         Use this for receiving packets from unknown sender crypto version.
   *
   * \param  shared_secret  32-byte shared secret from key exchange
   * \param  dest  destination buffer for decrypted plaintext
   * \param  src  encrypted data
   * \param  src_len  length of encrypted data
   * \param  was_ascon  optional output: set to true if Ascon decryption succeeded, false if legacy
   * \returns  plaintext length on success, 0 on authentication failure
  */
  static int decryptAuto(const uint8_t* shared_secret, uint8_t* dest,
                         const uint8_t* src, int src_len, bool* was_ascon = nullptr);

  /**
   * \brief  Get hardware random bytes from platform-specific TRNG.
   * \param  dest  destination buffer
   * \param  size  number of random bytes to generate
  */
  static void getHardwareRandom(uint8_t* dest, size_t size);

  /**
   * \brief  Initialize the Ascon counter with a random starting value.
   *         Call this once at boot before any encryption operations.
   *         Provides defense-in-depth against counter reuse across reboots,
   *         especially when timestamp replay protection is unreliable.
   * \param  rng  random number generator
  */
  static void initAsconCounter(RNG& rng);

  /**
   * \brief  Generate a 4-byte counter for Ascon nonce.
   *         Counter starts from random value (set by initAsconCounter) and increments.
   * \param  counter  destination buffer (must be ASCON_COUNTER_SIZE bytes)
  */
  static void generateCounter(uint8_t* counter);

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
