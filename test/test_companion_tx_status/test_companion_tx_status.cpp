#include <gtest/gtest.h>

#include <Dispatcher.h>
#include "../../examples/companion_radio/CompanionTxStatus.h"

TEST(CompanionTxStatus, MatchesPendingEntry) {
  const uint8_t stored_hash[] = {1, 2, 3, 4};
  const uint8_t packet_hash[] = {1, 2, 3, 4};

  EXPECT_TRUE(companion::isPendingTxMatch(42, companion::TX_STATUS_PENDING, stored_hash,
                                          packet_hash, sizeof(packet_hash)));
}

TEST(CompanionTxStatus, SkipsTimedOutDuplicateAndMatchesNewPendingEntry) {
  const uint8_t packet_hash[] = {1, 2, 3, 4};

  EXPECT_FALSE(companion::isPendingTxMatch(42, mesh::PACKET_TX_TIMEOUT, packet_hash,
                                           packet_hash, sizeof(packet_hash)));
  EXPECT_TRUE(companion::isPendingTxMatch(42, companion::TX_STATUS_PENDING, packet_hash,
                                          packet_hash, sizeof(packet_hash)));
}

TEST(CompanionTxStatus, SkipsClearedAckAndDifferentHash) {
  const uint8_t stored_hash[] = {1, 2, 3, 4};
  const uint8_t different_hash[] = {4, 3, 2, 1};

  EXPECT_FALSE(companion::isPendingTxMatch(0, companion::TX_STATUS_PENDING, stored_hash,
                                           stored_hash, sizeof(stored_hash)));
  EXPECT_FALSE(companion::isPendingTxMatch(42, companion::TX_STATUS_PENDING, stored_hash,
                                           different_hash, sizeof(stored_hash)));
}

TEST(CompanionTxStatus, PushesOnlyForProtocolV14AndNewer) {
  EXPECT_FALSE(companion::shouldPushTxStatus(13));
  EXPECT_TRUE(companion::shouldPushTxStatus(14));
  EXPECT_TRUE(companion::shouldPushTxStatus(15));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
