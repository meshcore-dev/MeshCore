#include <gtest/gtest.h>

#include "helpers/StaticPoolPacketManager.h"

// The native test environment only builds selected src files. Include this small
// implementation directly so the test can exercise queue ordering without
// widening the PlatformIO test build filter.
#include "../../src/helpers/StaticPoolPacketManager.cpp"

TEST(StaticPoolPacketManager, PeekNextOutboundReturnsBestDuePacketWithoutRemovingIt) {
    StaticPoolPacketManager manager(4);

    mesh::Packet* low_priority = manager.allocNew();
    mesh::Packet* high_priority = manager.allocNew();
    mesh::Packet* future = manager.allocNew();

    ASSERT_NE(nullptr, low_priority);
    ASSERT_NE(nullptr, high_priority);
    ASSERT_NE(nullptr, future);

    manager.queueOutbound(low_priority, 5, 100);
    manager.queueOutbound(high_priority, 1, 100);
    manager.queueOutbound(future, 0, 200);

    EXPECT_EQ(high_priority, manager.peekNextOutbound(100));
    EXPECT_EQ(3, manager.getOutboundTotal());

    EXPECT_EQ(high_priority, manager.getNextOutbound(100));
    EXPECT_EQ(2, manager.getOutboundTotal());
    EXPECT_EQ(low_priority, manager.peekNextOutbound(100));
}

TEST(StaticPoolPacketManager, PeekNextOutboundIgnoresFuturePackets) {
    StaticPoolPacketManager manager(2);

    mesh::Packet* future = manager.allocNew();
    ASSERT_NE(nullptr, future);

    manager.queueOutbound(future, 0, 200);

    EXPECT_EQ(nullptr, manager.peekNextOutbound(100));
    EXPECT_EQ(future, manager.peekNextOutbound(200));
    EXPECT_EQ(1, manager.getOutboundTotal());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
