#include <unity.h>
#include <string.h>
#include "test_utils.h"
#include "test_vectors.h"
#include <ed_25519.h>

/**
 * \brief Test: Ed25519 key generation with deterministic seed
 */

void test_ed25519_create_keypair_deterministic(void) {
  uint8_t seed[32];
  uint8_t pub1[32], prv1[64];
  uint8_t pub2[32], prv2[64];

  fill_test_data(seed, sizeof(seed), 0xdeadbeef);

  // Generate keypair twice from same seed
  ed25519_create_keypair(pub1, prv1, seed);
  ed25519_create_keypair(pub2, prv2, seed);

  // Keys should be identical
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pub1, pub2, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(prv1, prv2, 64);
}

void test_ed25519_create_keypair_different_seeds(void) {
  uint8_t seed1[32], seed2[32];
  uint8_t pub1[32], prv1[64];
  uint8_t pub2[32], prv2[64];

  fill_test_data(seed1, sizeof(seed1), 0x11111111);
  fill_test_data(seed2, sizeof(seed2), 0x22222222);

  ed25519_create_keypair(pub1, prv1, seed1);
  ed25519_create_keypair(pub2, prv2, seed2);

  // Keys should be different
  TEST_ASSERT_FALSE(buffers_equal(pub1, pub2, 32));
  TEST_ASSERT_FALSE(buffers_equal(prv1, prv2, 64));
}

/**
 * \brief Test: Ed25519 key exchange
 */

void test_ed25519_key_exchange_commutative(void) {
  uint8_t seed_a[32], seed_b[32];
  uint8_t pub_a[32], prv_a[64];
  uint8_t pub_b[32], prv_b[64];
  uint8_t shared_ab[32], shared_ba[32];

  fill_test_data(seed_a, sizeof(seed_a), 0xaaaaaaaa);
  fill_test_data(seed_b, sizeof(seed_b), 0xbbbbbbbb);

  // Generate keypairs for Alice and Bob
  ed25519_create_keypair(pub_a, prv_a, seed_a);
  ed25519_create_keypair(pub_b, prv_b, seed_b);

  // Alice computes: shared_secret = her_private × Bob's_public
  ed25519_key_exchange(shared_ab, pub_b, prv_a);

  // Bob computes: shared_secret = his_private × Alice's_public
  ed25519_key_exchange(shared_ba, pub_a, prv_b);

  // Both should arrive at same shared secret
  TEST_ASSERT_EQUAL_UINT8_ARRAY(shared_ab, shared_ba, 32);
}

void test_ed25519_key_exchange_different_peers(void) {
  uint8_t seed_a[32], seed_b[32], seed_c[32];
  uint8_t pub_a[32], prv_a[64];
  uint8_t pub_b[32], prv_b[64];
  uint8_t pub_c[32], prv_c[64];
  uint8_t shared_ab[32], shared_ac[32];

  fill_test_data(seed_a, sizeof(seed_a), 0x11111111);
  fill_test_data(seed_b, sizeof(seed_b), 0x22222222);
  fill_test_data(seed_c, sizeof(seed_c), 0x33333333);

  ed25519_create_keypair(pub_a, prv_a, seed_a);
  ed25519_create_keypair(pub_b, prv_b, seed_b);
  ed25519_create_keypair(pub_c, prv_c, seed_c);

  ed25519_key_exchange(shared_ab, pub_b, prv_a);
  ed25519_key_exchange(shared_ac, pub_c, prv_a);

  // Different peers should produce different shared secrets
  TEST_ASSERT_FALSE(buffers_equal(shared_ab, shared_ac, 32));
}

/**
 * \brief Test: Ed25519 sign/verify
 */

void test_ed25519_sign_verify_valid_signature(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];
  uint8_t message[] = "Hello, World!";
  uint8_t signature[64];

  fill_test_data(seed, sizeof(seed), 0xfeedface);

  ed25519_create_keypair(pub, prv, seed);
  ed25519_sign(signature, message, sizeof(message) - 1, pub, prv);

  int result = ed25519_verify(signature, message, sizeof(message) - 1, pub);
  TEST_ASSERT_EQUAL_INT(1, result);
}

void test_ed25519_sign_verify_invalid_signature_wrong_message(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];
  uint8_t message1[] = "Hello, World!";
  uint8_t message2[] = "Hello, World?";
  uint8_t signature[64];

  fill_test_data(seed, sizeof(seed), 0xfeedface);

  ed25519_create_keypair(pub, prv, seed);
  ed25519_sign(signature, message1, sizeof(message1) - 1, pub, prv);
  
  // Verify with different message should fail
  int result = ed25519_verify(signature, message2, sizeof(message2) - 1, pub);
  TEST_ASSERT_EQUAL_INT(0, result);
}

void test_ed25519_sign_verify_invalid_signature_tampered(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];
  uint8_t message[] = "Secret message";
  uint8_t signature[64];

  fill_test_data(seed, sizeof(seed), 0xdeadbeef);

  ed25519_create_keypair(pub, prv, seed);
  ed25519_sign(signature, message, sizeof(message) - 1, pub, prv);

  signature[0] ^= 0xFF;

  int result = ed25519_verify(signature, message, sizeof(message) - 1, pub);
  TEST_ASSERT_EQUAL_INT(0, result);
}

void test_ed25519_sign_verify_invalid_signature_wrong_key(void) {
  uint8_t seed1[32], seed2[32];
  uint8_t pub1[32], prv1[64];
  uint8_t pub2[32], prv2[64];
  uint8_t message[] = "Important data";
  uint8_t signature[64];

  fill_test_data(seed1, sizeof(seed1), 0xaaaaaaaa);
  fill_test_data(seed2, sizeof(seed2), 0xbbbbbbbb);

  ed25519_create_keypair(pub1, prv1, seed1);
  ed25519_create_keypair(pub2, prv2, seed2);

  // Sign with key1
  ed25519_sign(signature, message, sizeof(message) - 1, pub1, prv1);

  // Verify with key2 should fail
  int result = ed25519_verify(signature, message, sizeof(message) - 1, pub2);
  TEST_ASSERT_EQUAL_INT(0, result);
}

void test_ed25519_sign_verify_empty_message(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];
  uint8_t message[] = {};
  uint8_t signature[64];

  fill_test_data(seed, sizeof(seed), 0x12345678);

  ed25519_create_keypair(pub, prv, seed);
  ed25519_sign(signature, message, sizeof(message), pub, prv);

  int result = ed25519_verify(signature, message, sizeof(message), pub);
  TEST_ASSERT_EQUAL_INT(1, result);
}

void test_ed25519_sign_verify_long_message(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];
  uint8_t message[256];
  uint8_t signature[64];

  fill_test_data(seed, sizeof(seed), 0x99887766);
  fill_test_data(message, sizeof(message), 0x11223344);

  ed25519_create_keypair(pub, prv, seed);
  ed25519_sign(signature, message, sizeof(message), pub, prv);

  int result = ed25519_verify(signature, message, sizeof(message), pub);
  TEST_ASSERT_EQUAL_INT(1, result);
}

/**
 * \brief Test: Known test vector for Ed25519 keypair generation (regression detection)
 * 
 * This test verifies that the ed25519 keypair generation is stable and produces
 * the same output for a known seed. The expected public key is hardcoded to detect
 * any regressions in the implementation.
 */
void test_ed25519_keypair_known_vector(void) {
  uint8_t pub[32], prv[64];

  // Generate keypair from known seed
  ed25519_create_keypair(pub, prv, (uint8_t*)VECTOR_ED25519_SEED);

  // Must match expected hardcoded public key (regression detection)
  TEST_ASSERT_EQUAL_UINT8_ARRAY(VECTOR_ED25519_PUBLIC_KEY, pub, 32);

  // Generate again - must be identical (deterministic)
  uint8_t pub2[32], prv2[64];
  ed25519_create_keypair(pub2, prv2, (uint8_t*)VECTOR_ED25519_SEED);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, pub2, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(prv, prv2, 64);
}

/**
 * \brief Test: Known test vector for Ed25519 signature (regression detection)
 * 
 * This test verifies that ed25519 signature generation is stable and produces
 * the same signature for a known key and message. The expected signature is
 * hardcoded to detect any regressions.
 */
void test_ed25519_signature_known_vector(void) {
  uint8_t pub[32], prv[64];

  // Generate keypair from known seed
  ed25519_create_keypair(pub, prv, (uint8_t*)VECTOR_ED25519_SEED);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(VECTOR_ED25519_PUBLIC_KEY, pub, 32);

  // Sign known message
  uint8_t signature[64];
  ed25519_sign(signature, (uint8_t*)VECTOR_ED25519_MESSAGE, sizeof(VECTOR_ED25519_MESSAGE), pub, prv);

  // Must match expected hardcoded signature (regression detection)
  TEST_ASSERT_EQUAL_UINT8_ARRAY(VECTOR_ED25519_SIGNATURE, signature, 64);

  // Signature must verify
  int result = ed25519_verify(signature, (uint8_t*)VECTOR_ED25519_MESSAGE, sizeof(VECTOR_ED25519_MESSAGE), pub);
  TEST_ASSERT_EQUAL_INT(1, result);

  // Sign again - must produce identical signature (deterministic)
  uint8_t signature2[64];
  ed25519_sign(signature2, (uint8_t*)VECTOR_ED25519_MESSAGE, sizeof(VECTOR_ED25519_MESSAGE), pub, prv);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(signature, signature2, 64);
}

/**\n * \\brief Benchmark: Ed25519 key generation\n */

void test_ed25519_benchmark_create_keypair(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];

  fill_test_data(seed, sizeof(seed), 0xdeadbeef);

  BenchmarkTimer timer;
  const int iterations = 100;

  timer.start();
  for (int i = 0; i < iterations; i++) {
    // Use different seed for each iteration
    seed[0] = (uint8_t)(i & 0xFF);
    ed25519_create_keypair(pub, prv, seed);
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("Ed25519 create_keypair: %u ms for %d iterations (%.2f ms avg)", 
         elapsed, iterations, (elapsed * 1.0) / iterations);
}

/**
 * \brief Benchmark: Ed25519 key exchange
 */

void test_ed25519_benchmark_key_exchange(void) {
  uint8_t seed_a[32], seed_b[32];
  uint8_t pub_a[32], prv_a[64];
  uint8_t pub_b[32], prv_b[64];
  uint8_t shared[32];

  fill_test_data(seed_a, sizeof(seed_a), 0xaaaaaaaa);
  fill_test_data(seed_b, sizeof(seed_b), 0xbbbbbbbb);

  ed25519_create_keypair(pub_a, prv_a, seed_a);
  ed25519_create_keypair(pub_b, prv_b, seed_b);

  BenchmarkTimer timer;
  const int iterations = 500;

  timer.start();
  for (int i = 0; i < iterations; i++) {
    ed25519_key_exchange(shared, pub_b, prv_a);
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("Ed25519 key_exchange: %u ms for %d iterations (%.2f ms avg)", 
         elapsed, iterations, (elapsed * 1.0) / iterations);
}

/**
 * \brief Benchmark: Ed25519 signing
 */

void test_ed25519_benchmark_sign(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];
  uint8_t message[64];
  uint8_t signature[64];

  fill_test_data(seed, sizeof(seed), 0xfeedface);
  fill_test_data(message, sizeof(message), 0xdeadbeef);

  ed25519_create_keypair(pub, prv, seed);

  BenchmarkTimer timer;
  const int iterations = 200;

  timer.start();
  for (int i = 0; i < iterations; i++) {
    ed25519_sign(signature, message, sizeof(message), pub, prv);
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("Ed25519 sign: %u ms for %d iterations (%.2f ms avg)", 
         elapsed, iterations, (elapsed * 1.0) / iterations);
}

/**
 * \brief Benchmark: Ed25519 verification
 */

void test_ed25519_benchmark_verify(void) {
  uint8_t seed[32];
  uint8_t pub[32], prv[64];
  uint8_t message[64];
  uint8_t signature[64];

  fill_test_data(seed, sizeof(seed), 0xcafebabe);
  fill_test_data(message, sizeof(message), 0x12345678);

  ed25519_create_keypair(pub, prv, seed);
  ed25519_sign(signature, message, sizeof(message), pub, prv);

  BenchmarkTimer timer;
  const int iterations = 200;

  timer.start();
  for (int i = 0; i < iterations; i++) {
    ed25519_verify(signature, message, sizeof(message), pub);
  }
  timer.stop();

  uint32_t elapsed = timer.elapsed_ms();
  DBG_INFO("Ed25519 verify: %u ms for %d iterations (%.2f ms avg)", 
         elapsed, iterations, (elapsed * 1.0) / iterations);
}
