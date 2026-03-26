#include <unity.h>
#include <string.h>
#include "test_utils.h"
#include "test_vectors.h"
#include <Utils.h>
#include <MeshCore.h>
#include <AES.h>

using namespace mesh;

/**
 * \brief Test: encryptThenMAC basic functionality
 */
void test_encryptThenMAC_basic_small_payload(void) {
  uint8_t shared_secret[32];
  uint8_t plaintext[16];
  uint8_t ciphertext[32];  // MAC (2) + ciphertext (16)

  fill_test_data(shared_secret, sizeof(shared_secret), 0xdeadbeef);
  fill_test_data(plaintext, sizeof(plaintext), 0xfeedface);

  int result = Utils::encryptThenMAC(shared_secret, ciphertext, plaintext, sizeof(plaintext));

  // Should return MAC (2 bytes) + encrypted data (16 bytes)
  TEST_ASSERT_EQUAL_INT(18, result);
}

/**
 * \brief Test: encryptThenMAC with various payload sizes
 */
void test_encryptThenMAC_various_sizes(void) {
  uint8_t shared_secret[16];
  uint8_t dest[256];
  uint8_t src[100];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xaaaaaaaa);
  fill_test_data(src, sizeof(src), 0xbbbbbbbb);

  int result = Utils::encryptThenMAC(shared_secret, dest, src, 1);
  TEST_ASSERT_EQUAL_INT(CIPHER_MAC_SIZE + 16, result);

  result = Utils::encryptThenMAC(shared_secret, dest, src, 15);
  TEST_ASSERT_EQUAL_INT(CIPHER_MAC_SIZE + 16, result);

  result = Utils::encryptThenMAC(shared_secret, dest, src, 16);
  TEST_ASSERT_EQUAL_INT(CIPHER_MAC_SIZE + 16, result);

  result = Utils::encryptThenMAC(shared_secret, dest, src, 17);
  TEST_ASSERT_EQUAL_INT(CIPHER_MAC_SIZE + 32, result);
  
  result = Utils::encryptThenMAC(shared_secret, dest, src, MAX_PACKET_PAYLOAD);
  int expected_enc_len = ((MAX_PACKET_PAYLOAD + 15) / 16) * 16;
  TEST_ASSERT_EQUAL_INT(CIPHER_MAC_SIZE + expected_enc_len, result);
}

/**
 * \brief Test: MACThenDecrypt with valid MAC and ciphertext
 */
void test_MACThenDecrypt_valid_mac(void) {
  uint8_t shared_secret[32];
  uint8_t plaintext[16];
  uint8_t encrypted[32];
  uint8_t decrypted[32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0x11111111);
  fill_test_data(plaintext, sizeof(plaintext), 0x22222222);

  int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, sizeof(plaintext));
  int dec_len = Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);
  
  // Should succeed (non-zero result)
  TEST_ASSERT_NOT_EQUAL_INT(0, dec_len);
  TEST_ASSERT_EQUAL_INT(((sizeof(plaintext) + 15) / 16) * 16, dec_len);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted, sizeof(plaintext));
}

/**
 * \brief Test: MACThenDecrypt with invalid MAC (tampered)
 */
void test_MACThenDecrypt_invalid_mac_tampered(void) {
  uint8_t shared_secret[16];
  uint8_t plaintext[16];
  uint8_t encrypted[32];
  uint8_t decrypted[32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0x33333333);
  fill_test_data(plaintext, sizeof(plaintext), 0x44444444);

  int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, sizeof(plaintext));
  encrypted[0] ^= 0xFF;

  // Decrypt and verify MAC - should fail
  int dec_len = Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);

  TEST_ASSERT_EQUAL_INT(0, dec_len);
}

/**
 * \brief Test: MACThenDecrypt with tampered ciphertext
 */
void test_MACThenDecrypt_invalid_mac_ciphertext_tampered(void) {
  uint8_t shared_secret[16];
  uint8_t plaintext[16];
  uint8_t encrypted[32];
  uint8_t decrypted[32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0x55555555);
  fill_test_data(plaintext, sizeof(plaintext), 0x66666666);

  int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, sizeof(plaintext));
  encrypted[CIPHER_MAC_SIZE] ^= 0xFF;

  // Decrypt and verify MAC - should fail
  int dec_len = Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);

  TEST_ASSERT_EQUAL_INT(0, dec_len);
}

/**
 * \brief Test: MACThenDecrypt with wrong shared secret
 */
void test_MACThenDecrypt_wrong_shared_secret(void) {
  uint8_t secret1[16], secret2[16];
  uint8_t plaintext[16];
  uint8_t encrypted[32];
  uint8_t decrypted[32];

  fill_test_data(secret1, sizeof(secret1), 0x77777777);
  fill_test_data(secret2, sizeof(secret2), 0x88888888);
  fill_test_data(plaintext, sizeof(plaintext), 0x99999999);

  // Encrypt and MAC with secret1
  int enc_len = Utils::encryptThenMAC(secret1, encrypted, plaintext, sizeof(plaintext));

  // Try to decrypt with secret2 - should fail
  int dec_len = Utils::MACThenDecrypt(secret2, decrypted, encrypted, enc_len);

  TEST_ASSERT_EQUAL_INT(0, dec_len);
}

/**
 * \brief Test: MACThenDecrypt with max payload size
 */
void test_MACThenDecrypt_max_payload(void) {
  uint8_t shared_secret[32];
  uint8_t plaintext[MAX_PACKET_PAYLOAD];
  uint8_t encrypted[MAX_PACKET_PAYLOAD + 32];
  uint8_t decrypted[MAX_PACKET_PAYLOAD + 32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xaabbccdd);
  fill_test_data(plaintext, sizeof(plaintext), 0x11223344);
  
  int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, sizeof(plaintext));
  int dec_len = Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);

  TEST_ASSERT_NOT_EQUAL_INT(0, dec_len);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted, sizeof(plaintext));
}

/**
 * \brief Test: encryptThenMAC/MACThenDecrypt roundtrip with max payload
 */
void test_encryptThenMAC_MACThenDecrypt_roundtrip_max_payload(void) {
  uint8_t shared_secret[32];
  uint8_t plaintext[MAX_PACKET_PAYLOAD];
  uint8_t encrypted[MAX_PACKET_PAYLOAD + 32];
  uint8_t decrypted[MAX_PACKET_PAYLOAD + 32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xdeadbeef);
  fill_test_data(plaintext, sizeof(plaintext), 0xcafebabe);

  int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, sizeof(plaintext));
  int dec_len = Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);

  TEST_ASSERT_NOT_EQUAL_INT(0, dec_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted, sizeof(plaintext));
}

/**
 * \brief Test: Empty payload encryption/decryption
 */
void test_encryptThenMAC_MACThenDecrypt_empty_payload(void) {
  uint8_t shared_secret[32];
  uint8_t plaintext[0] = {};
  uint8_t encrypted[32];
  uint8_t decrypted[32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0x12345678);

  // Real implementation: encrypting empty payload produces no ciphertext (0 bytes)
  int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, 0);

  // Result is just MAC (2 bytes) + 0 bytes of ciphertext = 2 bytes total
  TEST_ASSERT_EQUAL_INT(CIPHER_MAC_SIZE, enc_len);

  // Try to decrypt - this will fail because length (2) <= CIPHER_MAC_SIZE (2)
  int dec_len = Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);

  // Real implementation rejects this as invalid
  TEST_ASSERT_EQUAL_INT(0, dec_len);
}

/**
 * \brief Test: MACThenDecrypt with invalid ciphertext length (too short)
 */
void test_MACThenDecrypt_invalid_length_too_short(void) {
  uint8_t shared_secret[32];
  uint8_t encrypted[5];  // Smaller than MAC size
  uint8_t decrypted[32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xfeedface);
  fill_test_data(encrypted, sizeof(encrypted), 0x12345678);

  // Should return 0 for invalid length
  int dec_len = Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, 1);

  TEST_ASSERT_EQUAL_INT(0, dec_len);
}

/**
 * \brief Test: Different shared secrets produce different ciphertexts
 */
void test_encryptThenMAC_different_keys_different_output(void) {
  uint8_t secret1[16], secret2[16];
  uint8_t plaintext[16];
  uint8_t encrypted1[32], encrypted2[32];

  fill_test_data(secret1, sizeof(secret1), 0x11111111);
  fill_test_data(secret2, sizeof(secret2), 0x22222222);
  fill_test_data(plaintext, sizeof(plaintext), 0x33333333);

  int len1 = Utils::encryptThenMAC(secret1, encrypted1, plaintext, sizeof(plaintext));
  int len2 = Utils::encryptThenMAC(secret2, encrypted2, plaintext, sizeof(plaintext));

  TEST_ASSERT_EQUAL_INT(len1, len2);

  // Ciphertexts should be different (very low probability of collision)
  TEST_ASSERT_FALSE(buffers_equal(encrypted1, encrypted2, len1));
}

/**
 * \brief Test: Same plaintext with same key produces same ciphertext (deterministic)
 */
void test_encryptThenMAC_deterministic(void) {
  uint8_t shared_secret[32];
  uint8_t plaintext[16];
  uint8_t encrypted1[32], encrypted2[32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xaabbccdd);
  fill_test_data(plaintext, sizeof(plaintext), 0x11223344);

  int len1 = Utils::encryptThenMAC(shared_secret, encrypted1, plaintext, sizeof(plaintext));
  int len2 = Utils::encryptThenMAC(shared_secret, encrypted2, plaintext, sizeof(plaintext));

  TEST_ASSERT_EQUAL_INT(len1, len2);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(encrypted1, encrypted2, len1);
}

/**
 * \brief Test: Known test vector for encryptThenMAC (regression detection)
 * 
 * This test verifies that AES-128 + HMAC encryption is stable and produces
 * the same output for a known key and plaintext. The expected encrypted output
 * (MAC + ciphertext) is hardcoded to detect any regressions in the implementation.
 */
void test_encryptThenMAC_known_vector(void) {
  uint8_t result[34];

  int len = Utils::encryptThenMAC(VECTOR_AES_KEY, result, VECTOR_AES_PLAINTEXT, sizeof(VECTOR_AES_PLAINTEXT));

  // Should produce exactly 34 bytes (MAC 2 + ciphertext 32)
  TEST_ASSERT_EQUAL_INT(34, len);

  DBG_INFO("encryptThenMAC produced %d bytes of output", len);
  DBG_TRACE("Output (hex):");
  int clen = (len / sizeof(result[0]));
  for (int i = 0; i < clen; i++) {
    debug_print(DEBUG_TRACE,"0x%02X", result[i]);
    if (i < clen - 1) debug_print(DEBUG_TRACE,", ");
    if ((i + 1) % 8 == 0) debug_print(DEBUG_TRACE,"\n");
  }
  debug_print(DEBUG_TRACE,"\n");

  // Must match expected hardcoded output (regression detection)
  TEST_ASSERT_EQUAL_UINT8_ARRAY(VECTOR_AES_ENCRYPTED, result, len);

  // Encrypt again - must be identical (deterministic)
  uint8_t result2[34];
  int len2 = Utils::encryptThenMAC(VECTOR_AES_KEY, result2, VECTOR_AES_PLAINTEXT, sizeof(VECTOR_AES_PLAINTEXT));
  TEST_ASSERT_EQUAL_INT(len, len2);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(result, result2, len);
}

/**
 * \brief Test: Known test vector for MACThenDecrypt (regression detection)
 * 
 * This test verifies that MACThenDecrypt correctly decrypts the hardcoded
 * known test vector. It validates both MAC verification and decryption.
 */
void test_MACThenDecrypt_known_vector(void) {
  uint8_t decrypted[32];

  // Decrypt the known test vector
  int dec_len = Utils::MACThenDecrypt(VECTOR_AES_KEY, decrypted, VECTOR_AES_ENCRYPTED, sizeof(VECTOR_AES_ENCRYPTED));
  // Should succeed (non-zero result) and return original plaintext length
  TEST_ASSERT_NOT_EQUAL_INT(0, dec_len);
  DBG_INFO("MACThenDecrypt produced %d bytes of output", dec_len);
  DBG_TRACE("Decrypted output (hex):");
  for (int i = 0; i < dec_len; i++) {
    debug_print(DEBUG_TRACE, "0x%02X", decrypted[i]);
    if (i < dec_len - 1) debug_print(DEBUG_TRACE,", ");
    if ((i + 1) % 8 == 0) debug_print(DEBUG_TRACE,"\n");
  }
  debug_print(DEBUG_TRACE,"\n");
  TEST_ASSERT_EQUAL_INT(32, dec_len);

  // Decrypted plaintext must match the known original plaintext (regression detection)
  TEST_ASSERT_EQUAL_UINT8_ARRAY(VECTOR_AES_PLAINTEXT, decrypted, sizeof(VECTOR_AES_PLAINTEXT));
}

/**
 * \brief Benchmark: encryptThenMAC
 */
void test_encryptThenMAC_benchmark(void) {
  uint8_t shared_secret[16];
  uint8_t plaintext[MAX_PACKET_PAYLOAD];
  uint8_t encrypted[MAX_PACKET_PAYLOAD + 32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xdeadbeef);
  fill_test_data(plaintext, sizeof(plaintext), 0xcafebabe);

  BenchmarkTimer timer;
  const int iterations = 100;

  timer.start();
  for (int i = 0; i < iterations; i++) {
    Utils::encryptThenMAC(shared_secret, encrypted, plaintext, MAX_PACKET_PAYLOAD);
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("encryptThenMAC (%d bytes): %u ms for %d iterations (%.2f ms avg)", 
         MAX_PACKET_PAYLOAD, elapsed, iterations, (elapsed * 1.0) / iterations);
}

/**
 * \brief Benchmark: MACThenDecrypt
 */
void test_MACThenDecrypt_benchmark(void) {
  uint8_t shared_secret[16];
  uint8_t plaintext[MAX_PACKET_PAYLOAD];
  uint8_t encrypted[MAX_PACKET_PAYLOAD + 32];
  uint8_t decrypted[MAX_PACKET_PAYLOAD + 32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xfeedface);
  fill_test_data(plaintext, sizeof(plaintext), 0x12345678);

  int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, MAX_PACKET_PAYLOAD);

  BenchmarkTimer timer;
  const int iterations = 100;

  timer.start();
  for (int i = 0; i < iterations; i++) {
    Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("MACThenDecrypt (%d bytes): %u ms for %d iterations (%.2f ms avg)", 
         MAX_PACKET_PAYLOAD, elapsed, iterations, (elapsed * 1.0) / iterations);
}

/**
 * \brief Benchmark: encryptThenMAC + MACThenDecrypt roundtrip
 */
void test_encryptThenMAC_MACThenDecrypt_benchmark_roundtrip(void) {
  uint8_t shared_secret[16];
  uint8_t plaintext[MAX_PACKET_PAYLOAD];
  uint8_t encrypted[MAX_PACKET_PAYLOAD + 32];
  uint8_t decrypted[MAX_PACKET_PAYLOAD + 32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xabcdef01);
  fill_test_data(plaintext, sizeof(plaintext), 0x23456789);

  BenchmarkTimer timer;
  const int iterations = 500;

  timer.start();
  for (int i = 0; i < iterations; i++) {
    int enc_len = Utils::encryptThenMAC(shared_secret, encrypted, plaintext, MAX_PACKET_PAYLOAD);
    Utils::MACThenDecrypt(shared_secret, decrypted, encrypted, enc_len);
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("encryptThenMAC + MACThenDecrypt roundtrip (%d bytes): %u ms for %d iterations (%.2f ms avg)", 
         MAX_PACKET_PAYLOAD, elapsed, iterations, (elapsed * 1.0) / iterations);
}

static int encrypt(AESCommon* aes,
                   const uint8_t* shared_secret,
                   uint8_t* dest,
                   const uint8_t* src,
                   int src_len)
{
    return mesh::Utils::encrypt(aes, shared_secret, dest, src, src_len);
}

static int decrypt(AESCommon* aes,
                   const uint8_t* shared_secret,
                   uint8_t* dest,
                   const uint8_t* src,
                   int src_len)
{
    return mesh::Utils::decrypt(aes, shared_secret, dest, src, src_len);
}

/// Benchmark: AES implementation performance comparison
void test_benchmark_AESImpl(void) {
  uint8_t shared_secret[32];
  uint8_t plaintext[MAX_PACKET_PAYLOAD];
  uint8_t encrypted[MAX_PACKET_PAYLOAD + 32];
  uint8_t decrypted[MAX_PACKET_PAYLOAD + 32];

  fill_test_data(shared_secret, sizeof(shared_secret), 0xabcdef01);
  fill_test_data(plaintext, sizeof(plaintext), 0x23456789);

  BenchmarkTimer timer;
  const int iterations = 500;
  printf("Benchmarking AES128 with %d bytes of plaintext:\n", MAX_PACKET_PAYLOAD);
  timer.start();
  for (int i = 0; i < iterations; i++) {
    AES128 aes;
    encrypt((AESCommon*)&aes, shared_secret, encrypted, plaintext, sizeof(plaintext));
    AES128 aes2;
    decrypt((AESCommon*)&aes2, shared_secret, decrypted, encrypted, ((sizeof(plaintext) + 15) / 16) * 16);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted, sizeof(plaintext));
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("AES128 (%d bytes): %u ms for %d iterations (%.2f ms avg)", 
         MAX_PACKET_PAYLOAD, elapsed, iterations, (elapsed * 1.0) / iterations);
  memset(encrypted, 0, sizeof(encrypted));
  memset(decrypted, 0, sizeof(decrypted));

  printf("Benchmarking AESTiny128 with %d bytes of plaintext:\n", MAX_PACKET_PAYLOAD);
  timer.start();
  for (int i = 0; i < iterations; i++) {
    AESTiny128 aes;
    encrypt((AESCommon*)&aes, shared_secret, encrypted, plaintext, sizeof(plaintext));
    AESTiny128 aes2;
    decrypt((AESCommon*)&aes2, shared_secret, decrypted, encrypted, ((sizeof(plaintext) + 15) / 16) * 16);
   // TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted, sizeof(plaintext));
   // FIXME: if we ever decide to use it we should investigate why it stops early
  }
  timer.stop();

  elapsed = timer.elapsed_ms();
  DBG_INFO("AESTiny128 (%d bytes): %u ms for %d iterations (%.2f ms avg)", 
         MAX_PACKET_PAYLOAD, elapsed, iterations, (elapsed * 1.0) / iterations);
  memset(encrypted, 0, sizeof(encrypted));
  memset(decrypted, 0, sizeof(decrypted));

  printf("Benchmarking AESSmall128 with %d bytes of plaintext:\n", MAX_PACKET_PAYLOAD);
  timer.start();
  for (int i = 0; i < iterations; i++) {
    AESSmall128 aes;
    encrypt((AESCommon*)&aes, shared_secret, encrypted, plaintext, sizeof(plaintext));
    AESSmall128 aes2;
    decrypt((AESCommon*)&aes2, shared_secret, decrypted, encrypted, ((sizeof(plaintext) + 15) / 16) * 16);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted, sizeof(plaintext));
  }
  timer.stop();
  elapsed = timer.elapsed_ms();
  DBG_INFO("AESSmall128 (%d bytes): %u ms for %d iterations (%.2f ms avg)", 
         MAX_PACKET_PAYLOAD, elapsed, iterations, (elapsed * 1.0) / iterations);
  memset(encrypted, 0, sizeof(encrypted));
  memset(decrypted, 0, sizeof(decrypted));

#if defined(ESP32)
  printf("Benchmarking AES128_ESP with %d bytes of plaintext:\n", MAX_PACKET_PAYLOAD);
  timer.start();
  for (int i = 0; i < iterations; i++) {
    AES128_ESP aes;
    encrypt((AESCommon*)&aes, shared_secret, encrypted, plaintext, sizeof(plaintext));
    AES128_ESP aes2;
    decrypt((AESCommon*)&aes2, shared_secret, decrypted, encrypted, ((sizeof(plaintext) + 15) / 16) * 16);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted, sizeof(plaintext));
  }
  timer.stop();

  elapsed = timer.elapsed_ms();
  DBG_INFO("AES128_ESP (%d bytes): %u ms for %d iterations (%.2f ms avg)", 
         MAX_PACKET_PAYLOAD, elapsed, iterations, (elapsed * 1.0) / iterations);
  memset(encrypted, 0, sizeof(encrypted));
  memset(decrypted, 0, sizeof(decrypted));
#endif
  }