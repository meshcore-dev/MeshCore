#pragma once

// Common test declarations for both Arduino and native platforms
// This header eliminates duplication of test function declarations

// Test forward declarations - ED25519 tests
extern void test_ed25519_create_keypair_deterministic(void);
extern void test_ed25519_create_keypair_different_seeds(void);
extern void test_ed25519_key_exchange_commutative(void);
extern void test_ed25519_key_exchange_different_peers(void);
extern void test_ed25519_sign_verify_valid_signature(void);
extern void test_ed25519_sign_verify_invalid_signature_wrong_message(void);
extern void test_ed25519_sign_verify_invalid_signature_tampered(void);
extern void test_ed25519_sign_verify_invalid_signature_wrong_key(void);
extern void test_ed25519_sign_verify_empty_message(void);
extern void test_ed25519_sign_verify_long_message(void);
extern void test_ed25519_benchmark_create_keypair(void);
extern void test_ed25519_benchmark_key_exchange(void);
extern void test_ed25519_benchmark_sign(void);
extern void test_ed25519_benchmark_verify(void);
extern void test_ed25519_keypair_known_vector(void);
extern void test_ed25519_signature_known_vector(void);

// Test forward declarations - Utils crypto tests
extern void test_encryptThenMAC_basic_small_payload(void);
extern void test_encryptThenMAC_various_sizes(void);
extern void test_MACThenDecrypt_valid_mac(void);
extern void test_MACThenDecrypt_invalid_mac_tampered(void);
extern void test_MACThenDecrypt_invalid_mac_ciphertext_tampered(void);
extern void test_MACThenDecrypt_wrong_shared_secret(void);
extern void test_MACThenDecrypt_max_payload(void);
extern void test_encryptThenMAC_MACThenDecrypt_roundtrip_max_payload(void);
extern void test_encryptThenMAC_MACThenDecrypt_empty_payload(void);
extern void test_MACThenDecrypt_invalid_length_too_short(void);
extern void test_encryptThenMAC_different_keys_different_output(void);
extern void test_encryptThenMAC_deterministic(void);
extern void test_encryptThenMAC_known_vector(void);
extern void test_MACThenDecrypt_known_vector(void);
extern void test_encryptThenMAC_benchmark(void);
extern void test_MACThenDecrypt_benchmark(void);
extern void test_encryptThenMAC_MACThenDecrypt_benchmark_roundtrip(void);
extern void test_benchmark_AESImpl(void);

/**
 * \brief Register all tests with the test framework
 * Call this in your main() function after UNITY_BEGIN()
 */
inline void run_all_tests(void) {
  // ED25519 tests
  RUN_TEST(test_ed25519_create_keypair_deterministic);
  RUN_TEST(test_ed25519_create_keypair_different_seeds);
  RUN_TEST(test_ed25519_key_exchange_commutative);
  RUN_TEST(test_ed25519_key_exchange_different_peers);
  RUN_TEST(test_ed25519_sign_verify_valid_signature);
  RUN_TEST(test_ed25519_sign_verify_invalid_signature_wrong_message);
  RUN_TEST(test_ed25519_sign_verify_invalid_signature_tampered);
  RUN_TEST(test_ed25519_sign_verify_invalid_signature_wrong_key);
  RUN_TEST(test_ed25519_sign_verify_empty_message);
  RUN_TEST(test_ed25519_sign_verify_long_message);
  RUN_TEST(test_ed25519_keypair_known_vector);
  RUN_TEST(test_ed25519_signature_known_vector);
  RUN_TEST(test_ed25519_benchmark_create_keypair);
  RUN_TEST(test_ed25519_benchmark_key_exchange);
  RUN_TEST(test_ed25519_benchmark_sign);
  RUN_TEST(test_ed25519_benchmark_verify);

  // Utils crypto tests
  RUN_TEST(test_encryptThenMAC_basic_small_payload);
  RUN_TEST(test_encryptThenMAC_various_sizes);
  RUN_TEST(test_MACThenDecrypt_valid_mac);
  RUN_TEST(test_MACThenDecrypt_invalid_mac_tampered);
  RUN_TEST(test_MACThenDecrypt_invalid_mac_ciphertext_tampered);
  RUN_TEST(test_MACThenDecrypt_wrong_shared_secret);
  RUN_TEST(test_MACThenDecrypt_max_payload);
  RUN_TEST(test_encryptThenMAC_MACThenDecrypt_roundtrip_max_payload);
  RUN_TEST(test_encryptThenMAC_MACThenDecrypt_empty_payload);
  RUN_TEST(test_MACThenDecrypt_invalid_length_too_short);
  RUN_TEST(test_encryptThenMAC_different_keys_different_output);
  RUN_TEST(test_encryptThenMAC_deterministic);
  RUN_TEST(test_encryptThenMAC_known_vector);
  RUN_TEST(test_MACThenDecrypt_known_vector);
  RUN_TEST(test_encryptThenMAC_benchmark);
  RUN_TEST(test_MACThenDecrypt_benchmark);
  RUN_TEST(test_encryptThenMAC_MACThenDecrypt_benchmark_roundtrip);
  RUN_TEST(test_benchmark_AESImpl);
}
