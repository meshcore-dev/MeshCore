#include <gtest/gtest.h>

#include <helpers/ClientPathObservation.h>

namespace {

static constexpr uint8_t UNKNOWN_PATH = 0xFF;

struct ClientState {
  uint8_t out_path_len = UNKNOWN_PATH;
  uint8_t out_path[64] = {};
  uint8_t observed_path_len = UNKNOWN_PATH;
  uint8_t observed_path[64] = {};
  bool observed_path_pending = false;
  uint32_t observed_path_expiry = 0;
};

TEST(ClientPathObservation, CaptureDoesNotChangeSelectedOutputPath) {
  ClientState client;
  client.out_path_len = 1;
  client.out_path[0] = 0xAA;
  const uint8_t received[] = {0x10, 0x20, 0x30, 0x40};

  mesh::beginObservedClientPath(client, UNKNOWN_PATH, 1000);
  ASSERT_TRUE(mesh::captureObservedClientPath(
      client, received, 0x42, 500));  // two 2-byte hashes
  EXPECT_EQ(client.observed_path_len, 0x42);
  EXPECT_EQ(memcmp(client.observed_path, received, sizeof(received)), 0);
  EXPECT_EQ(client.out_path_len, 1);
  EXPECT_EQ(client.out_path[0], 0xAA);
}

TEST(ClientPathObservation, PromoteCopiesObservedPathToOutputPath) {
  ClientState client;
  const uint8_t received[] = {0x10, 0x20, 0x30, 0x40};
  mesh::beginObservedClientPath(client, UNKNOWN_PATH, 1000);
  ASSERT_TRUE(mesh::captureObservedClientPath(client, received, 0x42, 500));

  ASSERT_TRUE(mesh::promoteObservedClientPath(client));
  EXPECT_EQ(client.out_path_len, 0x42);
  EXPECT_EQ(memcmp(client.out_path, received, sizeof(received)), 0);
}

TEST(ClientPathObservation, MissingObservedPathCannotReplaceOutputPath) {
  ClientState client;
  client.out_path_len = 1;
  client.out_path[0] = 0xAA;

  EXPECT_FALSE(mesh::promoteObservedClientPath(client));
  EXPECT_EQ(client.out_path_len, 1);
  EXPECT_EQ(client.out_path[0], 0xAA);
}

TEST(ClientPathObservation, ZeroHopDirectPathCanBeObservedAndPromoted) {
  ClientState client;
  mesh::beginObservedClientPath(client, UNKNOWN_PATH, 1000);
  ASSERT_TRUE(mesh::captureObservedClientPath(client, nullptr, 0, 500));
  ASSERT_TRUE(mesh::promoteObservedClientPath(client));
  EXPECT_EQ(client.out_path_len, 0);
}

TEST(ClientPathObservation, NewLoginClearsPreviousObservation) {
  ClientState client;
  client.observed_path_len = 1;
  client.observed_path[0] = 0x55;

  mesh::clearObservedClientPath(client, UNKNOWN_PATH);
  EXPECT_EQ(client.observed_path_len, UNKNOWN_PATH);
  EXPECT_FALSE(client.observed_path_pending);
  EXPECT_EQ(client.observed_path_expiry, 0U);
  for (uint8_t byte : client.observed_path) EXPECT_EQ(byte, 0);
}

TEST(ClientPathObservation, NewFloodLoginWaitsForOneReciprocalPath) {
  ClientState client;
  const uint8_t first[] = {0x11};
  const uint8_t later[] = {0x22};

  mesh::beginObservedClientPath(client, UNKNOWN_PATH, 1000);
  EXPECT_TRUE(client.observed_path_pending);
  ASSERT_TRUE(mesh::captureObservedClientPath(client, first, 1, 500));
  EXPECT_FALSE(client.observed_path_pending);
  EXPECT_EQ(client.observed_path[0], 0x11);

  EXPECT_FALSE(mesh::captureObservedClientPath(client, later, 1, 600));
  EXPECT_EQ(client.observed_path[0], 0x11);
}

TEST(ClientPathObservation, PendingLoginPathExpires) {
  ClientState client;
  const uint8_t received[] = {0x11};

  mesh::beginObservedClientPath(client, UNKNOWN_PATH, 1000);
  EXPECT_TRUE(mesh::isObservedClientPathPending(client, 999));
  EXPECT_FALSE(mesh::captureObservedClientPath(client, received, 1, 1000));
  EXPECT_FALSE(client.observed_path_pending);
  EXPECT_EQ(client.observed_path_len, UNKNOWN_PATH);
}

} // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
