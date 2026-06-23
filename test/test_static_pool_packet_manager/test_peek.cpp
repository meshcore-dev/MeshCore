#include <gtest/gtest.h>

#include "helpers/StaticPoolPacketManager.h"

// The native test environment only builds selected src files. Include this small
// implementation directly so the test can exercise queue ordering without
// widening the PlatformIO test build filter.
#include "../../src/helpers/StaticPoolPacketManager.cpp"

namespace mesh {
Packet::Packet() {
    header = 0;
    path_len = 0;
    payload_len = 0;
}
}

TEST(PacketQueue, PeekReturnsBestDuePacketWithoutRemovingIt) {
    PacketQueue queue(4);
    mesh::Packet* low_priority = reinterpret_cast<mesh::Packet*>(0x01);
    mesh::Packet* high_priority = reinterpret_cast<mesh::Packet*>(0x02);
    mesh::Packet* future = reinterpret_cast<mesh::Packet*>(0x03);

    ASSERT_TRUE(queue.add(low_priority, 5, 100));
    ASSERT_TRUE(queue.add(high_priority, 1, 100));
    ASSERT_TRUE(queue.add(future, 0, 200));

    EXPECT_EQ(high_priority, queue.peek(100));
    EXPECT_EQ(3, queue.count());

    EXPECT_EQ(high_priority, queue.get(100));
    EXPECT_EQ(2, queue.count());
    EXPECT_EQ(low_priority, queue.peek(100));
}

TEST(PacketQueue, PeekIgnoresFuturePackets) {
    PacketQueue queue(2);
    mesh::Packet* future = reinterpret_cast<mesh::Packet*>(0x01);

    ASSERT_TRUE(queue.add(future, 0, 200));

    EXPECT_EQ(nullptr, queue.peek(100));
    EXPECT_EQ(future, queue.peek(200));
    EXPECT_EQ(1, queue.count());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
