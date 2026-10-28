#include <gtest/gtest.h>
#include "helpers/SimpleMeshTables.h"

using namespace mesh;

// Build a packet that calculatePacketHash() distinguishes by payload content.
// header selects ROUTE_TYPE_FLOOD so isRouteDirect() returns false.
static Packet makeFloodPacket(uint8_t seed) {
    Packet p;
    p.header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
    p.payload[0] = seed;
    p.payload_len = 1;
    p.path_len = 0;
    return p;
}

static Packet makeDirectPacket(uint8_t seed) {
    Packet p;
    p.header = ROUTE_TYPE_DIRECT | (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
    p.payload[0] = seed;
    p.payload_len = 1;
    p.path_len = 0;
    return p;
}

// ── wasSeen: pure query ───────────────────────────────────────────────────────

TEST(SimpleMeshTables, WasSeen_ReturnsFalseForUnseen) {
    SimpleMeshTables t;
    Packet p = makeFloodPacket(0x01);
    EXPECT_FALSE(t.wasSeen(&p));
}

// wasSeen shouldn't change state
TEST(SimpleMeshTables, WasSeen_IsPureQuery_DoesNotInsert) {
    SimpleMeshTables t;
    Packet p = makeFloodPacket(0x01);
    EXPECT_FALSE(t.wasSeen(&p));
    EXPECT_FALSE(t.wasSeen(&p));
}

// ── markSeen + wasSeen ───────────────────────────────────────────────────────

TEST(SimpleMeshTables, MarkSeen_MakesWasSeenReturnTrue) {
    SimpleMeshTables t;
    Packet p = makeFloodPacket(0x01);
    t.markSeen(&p);
    EXPECT_TRUE(t.wasSeen(&p));
}

TEST(SimpleMeshTables, MarkSeen_DoesNotAffectOtherPackets) {
    SimpleMeshTables t;
    Packet p1 = makeFloodPacket(0x01);
    Packet p2 = makeFloodPacket(0x02);
    t.markSeen(&p1);
    EXPECT_FALSE(t.wasSeen(&p2));
}

// Canonical pattern used at every onRecvPacket call site:
//   if (!wasSeen(pkt)) { markSeen(pkt); process(pkt); }
TEST(SimpleMeshTables, QueryThenMark_WorksCorrectly) {
    SimpleMeshTables t;
    Packet p = makeFloodPacket(0x01);
    EXPECT_FALSE(t.wasSeen(&p));
    t.markSeen(&p);
    EXPECT_TRUE(t.wasSeen(&p));
}

// ── dup stats ────────────────────────────────────────────────────────────────

TEST(SimpleMeshTables, WasSeen_IncrementsFloodDupStat) {
    SimpleMeshTables t;
    Packet p = makeFloodPacket(0x01);
    t.markSeen(&p);
    t.wasSeen(&p);
    EXPECT_EQ(1u, t.getNumFloodDups());
    EXPECT_EQ(0u, t.getNumDirectDups());
}

TEST(SimpleMeshTables, WasSeen_IncrementsDirectDupStat) {
    SimpleMeshTables t;
    Packet p = makeDirectPacket(0x01);
    t.markSeen(&p);
    t.wasSeen(&p);
    EXPECT_EQ(0u, t.getNumFloodDups());
    EXPECT_EQ(1u, t.getNumDirectDups());
}

// ── clear ────────────────────────────────────────────────────────────────────

TEST(SimpleMeshTables, Clear_RemovesSeenPacket) {
    SimpleMeshTables t;
    Packet p = makeFloodPacket(0x01);
    t.markSeen(&p);
    ASSERT_TRUE(t.wasSeen(&p));
    t.clear(&p);
    EXPECT_FALSE(t.wasSeen(&p));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
