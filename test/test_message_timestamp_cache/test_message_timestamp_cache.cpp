#include <gtest/gtest.h>

#include <helpers/MessageTimestampCache.h>

static void makeFingerprint(uint8_t fingerprint[MAX_HASH_SIZE], uint8_t value) {
  memset(fingerprint, value, MAX_HASH_SIZE);
}

TEST(MessageTimestampCache, ReturnsMappedTimestampForRetry) {
  mesh::MessageTimestampCache<4> cache;
  uint8_t fingerprint[MAX_HASH_SIZE];
  makeFingerprint(fingerprint, 0x11);

  ASSERT_TRUE(cache.remember(fingerprint, 100U, 500U));

  uint32_t mapped = 0;
  EXPECT_TRUE(cache.find(fingerprint, 100U, &mapped));
  EXPECT_EQ(500U, mapped);
}

TEST(MessageTimestampCache, DistinguishesLogicalMessages) {
  mesh::MessageTimestampCache<4> cache;
  uint8_t first[MAX_HASH_SIZE];
  uint8_t second[MAX_HASH_SIZE];
  makeFingerprint(first, 0x21);
  makeFingerprint(second, 0x22);

  ASSERT_TRUE(cache.remember(first, 100U, 500U));

  EXPECT_FALSE(cache.find(first, 101U));
  EXPECT_FALSE(cache.find(second, 100U));
}

TEST(MessageTimestampCache, UpdatesAnExistingMapping) {
  mesh::MessageTimestampCache<2> cache;
  uint8_t fingerprint[MAX_HASH_SIZE];
  makeFingerprint(fingerprint, 0x33);

  ASSERT_TRUE(cache.remember(fingerprint, 7U, 70U));
  ASSERT_TRUE(cache.remember(fingerprint, 7U, 71U));

  uint32_t mapped = 0;
  EXPECT_TRUE(cache.find(fingerprint, 7U, &mapped));
  EXPECT_EQ(71U, mapped);
}

TEST(MessageTimestampCache, ReplacesOldestEntryWhenFull) {
  mesh::MessageTimestampCache<2> cache;
  uint8_t first[MAX_HASH_SIZE];
  uint8_t second[MAX_HASH_SIZE];
  uint8_t third[MAX_HASH_SIZE];
  makeFingerprint(first, 0x41);
  makeFingerprint(second, 0x42);
  makeFingerprint(third, 0x43);

  ASSERT_TRUE(cache.remember(first, 1U, 101U));
  ASSERT_TRUE(cache.remember(second, 2U, 102U));
  ASSERT_TRUE(cache.remember(third, 3U, 103U));

  EXPECT_FALSE(cache.find(first, 1U));
  EXPECT_TRUE(cache.find(second, 2U));
  EXPECT_TRUE(cache.find(third, 3U));
}

TEST(MessageTimestampCache, RejectsNullFingerprint) {
  mesh::MessageTimestampCache<2> cache;

  EXPECT_FALSE(cache.remember(NULL, 1U, 2U));
  EXPECT_FALSE(cache.find(NULL, 1U));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
