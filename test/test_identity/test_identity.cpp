#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>

#define ED25519_NO_SEED 1
#include "Identity.h"
#include <ed_25519.h>

using namespace mesh;

class SeededRNG : public RNG {
  uint8_t val;
public:
  explicit SeededRNG(uint8_t start = 0) : val(start) {}
  void random(uint8_t* dest, size_t sz) override {
    for (size_t i = 0; i < sz; i++) dest[i] = val++;
  }
};

static LocalIdentity makeIdentity(uint8_t seed_start = 0) {
  SeededRNG rng(seed_start);
  return LocalIdentity(&rng);
}

TEST(Identity, SignVerifyRoundTrip) {
  LocalIdentity signer = makeIdentity();
  const uint8_t msg[] = "hello meshcore";
  uint8_t sig[SIGNATURE_SIZE];
  signer.sign(sig, msg, sizeof(msg) - 1);

  Identity verifier(signer.pub_key);
  EXPECT_TRUE(verifier.verify(sig, msg, sizeof(msg) - 1));
}

TEST(Identity, TamperedMessageFails) {
  LocalIdentity signer = makeIdentity();
  uint8_t msg[] = "hello meshcore";
  uint8_t sig[SIGNATURE_SIZE];
  signer.sign(sig, msg, sizeof(msg) - 1);

  msg[0] ^= 0x01;

  Identity verifier(signer.pub_key);
  EXPECT_FALSE(verifier.verify(sig, msg, sizeof(msg) - 1));
}

TEST(Identity, TamperedSignatureFails) {
  LocalIdentity signer = makeIdentity();
  const uint8_t msg[] = "hello meshcore";
  uint8_t sig[SIGNATURE_SIZE];
  signer.sign(sig, msg, sizeof(msg) - 1);

  sig[0] ^= 0x01;

  Identity verifier(signer.pub_key);
  EXPECT_FALSE(verifier.verify(sig, msg, sizeof(msg) - 1));
}

TEST(Identity, WrongPublicKeyFails) {
  LocalIdentity signer = makeIdentity(0);
  LocalIdentity other  = makeIdentity(64);
  const uint8_t msg[] = "hello meshcore";
  uint8_t sig[SIGNATURE_SIZE];
  signer.sign(sig, msg, sizeof(msg) - 1);

  Identity wrong_verifier(other.pub_key);
  EXPECT_FALSE(wrong_verifier.verify(sig, msg, sizeof(msg) - 1));
}

// Exercises the spinlock: 16 threads all call verify() simultaneously.
// Verifies results are correct under concurrent load (static buffers in ge.c
// would corrupt results without the reentrancy guard).
TEST(Identity, ConcurrentVerifyAllCorrect) {
  LocalIdentity signer = makeIdentity();
  const uint8_t msg[] = "concurrent verify test";
  uint8_t sig[SIGNATURE_SIZE];
  signer.sign(sig, msg, sizeof(msg) - 1);

  Identity verifier(signer.pub_key);
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;

  for (int i = 0; i < 16; i++) {
    threads.emplace_back([&]() {
      if (!verifier.verify(sig, msg, sizeof(msg) - 1))
        failures.fetch_add(1, std::memory_order_relaxed);
    });
  }
  for (auto& t : threads) t.join();

  EXPECT_EQ(0, failures.load());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
