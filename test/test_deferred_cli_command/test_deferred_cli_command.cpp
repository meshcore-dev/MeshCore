#include <gtest/gtest.h>

#include <helpers/DeferredCliCommand.h>

TEST(DeferredCliCommand, CopiesAuthenticatedCommandContext) {
  mesh::DeferredCliCommand deferred;
  uint8_t secret[PUB_KEY_SIZE];
  memset(secret, 0x5A, sizeof(secret));
  const char command[] = "del flood.moderation.all";

  ASSERT_TRUE(deferred.enqueue(7, 123456U, 2, secret, command, strlen(command)));
  EXPECT_TRUE(deferred.pending);
  EXPECT_EQ(7, deferred.client_index);
  EXPECT_EQ(123456U, deferred.sender_timestamp);
  EXPECT_EQ(2, deferred.path_hash_size);
  EXPECT_EQ(0, memcmp(secret, deferred.secret, sizeof(secret)));
  EXPECT_STREQ(command, deferred.command);

  secret[0] = 0;
  EXPECT_EQ(0x5A, deferred.secret[0]);
}

TEST(DeferredCliCommand, RejectsSecondCommandUntilCleared) {
  mesh::DeferredCliCommand deferred;
  uint8_t secret[PUB_KEY_SIZE] = {};
  const char first[] = "region save";
  const char second[] = "advert";

  ASSERT_TRUE(deferred.enqueue(1, 10U, 1, secret, first, strlen(first)));
  EXPECT_FALSE(deferred.enqueue(2, 11U, 3, secret, second, strlen(second)));
  EXPECT_EQ(1, deferred.client_index);
  EXPECT_STREQ(first, deferred.command);

  deferred.clear();
  EXPECT_FALSE(deferred.pending);
  EXPECT_EQ(0, deferred.secret[0]);
  EXPECT_EQ(0, deferred.command[0]);
  ASSERT_TRUE(deferred.enqueue(2, 11U, 3, secret, second, strlen(second)));
  EXPECT_STREQ(second, deferred.command);
}

TEST(DeferredCliCommand, RejectsAnOverlongCommand) {
  mesh::DeferredCliCommand deferred;
  uint8_t secret[PUB_KEY_SIZE] = {};
  char command[MAX_PACKET_PAYLOAD + 2];
  memset(command, 'x', sizeof(command));
  command[sizeof(command) - 1] = 0;

  EXPECT_FALSE(deferred.enqueue(0, 1U, 1, secret, command,
                                sizeof(deferred.command)));
  EXPECT_FALSE(deferred.pending);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
