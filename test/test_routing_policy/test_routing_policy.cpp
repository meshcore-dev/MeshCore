#include <gtest/gtest.h>
#include "helpers/RoutingPolicy.h"

using namespace mesh;

static Packet makeFlood(uint8_t route_type, uint8_t payload_type, uint8_t hops) {
    Packet p;
    p.header = route_type | (payload_type << PH_TYPE_SHIFT);
    p.setPathHashSizeAndCount(1, hops);
    p.payload_len = 1;
    if (route_type == ROUTE_TYPE_TRANSPORT_FLOOD) {
        p.transport_codes[0] = 1;  // has to be non-zero
    }
    return p;
}

TEST(FloodHopLimit, UnscopedFloodIsDroppedAtFirstHopWhenMaxUnscopedIsZero) {
    auto pkt = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&pkt, 64, 0, 8));
}

TEST(FloodHopLimit, ScopedFloodIsForwardedWhenMaxUnscopedIsZero) {
    for (uint8_t hops = 0; hops < 4; hops++) {
        auto pkt = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, hops);
        EXPECT_FALSE(isFloodHopLimitExceeded(&pkt, 64, 0, 8)) << "hops=" << (int)hops;
    }
}

TEST(FloodHopLimit, UnscopedFloodSurvivesUpToMaxUnscopedHops) {
    // matches the reported workaround: raising flood.max.unscoped to the expected hop count
    auto ok = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 2);
    EXPECT_FALSE(isFloodHopLimitExceeded(&ok, 64, 3, 8));

    auto too_far = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 3);
    EXPECT_TRUE(isFloodHopLimitExceeded(&too_far, 64, 3, 8));
}

TEST(FloodHopLimit, ScopedFloodStillHonoursFloodMaxAndAdvertMax) {
    auto beyond_max = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, 5);
    EXPECT_TRUE(isFloodHopLimitExceeded(&beyond_max, 5, 64, 8));

    auto advert = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 8);
    EXPECT_TRUE(isFloodHopLimitExceeded(&advert, 64, 64, 8));
}

// flood.max.unscoped=0 hits adverts too, well before flood_max_advert applies: a node
// still advertising un-scoped is invisible past its immediate neighbours
TEST(FloodHopLimit, UnscopedAdvertIsAlsoDroppedAtHopZero) {
    auto advert = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&advert, 64, 0, 8));

    auto scoped = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 0);
    EXPECT_FALSE(isFloodHopLimitExceeded(&scoped, 64, 0, 8));
}

TEST(ReplyRoute, FloodRequestGetsAPathReturn) {
    EXPECT_EQ(REPLY_ROUTE_PATH_RETURN,
              chooseReplyRoute(true, false, false));
    EXPECT_EQ(REPLY_ROUTE_PATH_RETURN,
              chooseReplyRoute(true, false, true));
}

TEST(ReplyRoute, DirectRequestWithSuppliedPathRepliesDirect) {
    EXPECT_EQ(REPLY_ROUTE_DIRECT_SUPPLIED, chooseReplyRoute(false, true, false));
}

// the reported bug: a DIRECT login (app already has a path) was answered by flooding, even
// with an out_path stored. Under flood.max.unscoped=0 that reply never arrives.
TEST(ReplyRoute, DirectRequestWithKnownOutPathRepliesDirect) {
    EXPECT_EQ(REPLY_ROUTE_DIRECT_OUT_PATH,
              chooseReplyRoute(false, false, true));
}

TEST(ReplyRoute, SuppliedPathWinsOverStoredOutPath) {
    EXPECT_EQ(REPLY_ROUTE_DIRECT_SUPPLIED, chooseReplyRoute(false, true, true));
}

TEST(ReplyRoute, DirectRequestWithNoReturnPathFallsBackToFlood) {
    EXPECT_EQ(REPLY_ROUTE_FLOOD, chooseReplyRoute(false, false, false));
}

TEST(ReplyScope, MirrorsTheRequestScopeWhenKnown) {
    EXPECT_EQ(REPLY_SCOPE_REQUEST, chooseReplyScope(true,
                                                    false,
                                                    false));
    EXPECT_EQ(REPLY_SCOPE_REQUEST, chooseReplyScope(true, false, true));
}

// un-scoped is itself a known scope, so mirror it. Replying scoped would change a path that
// works today, and repeaters not holding our default Region would drop it anyway.
TEST(ReplyScope, RepliesUnscopedToAnUnscopedFloodEvenWhenADefaultScopeExists) {
    EXPECT_EQ(REPLY_SCOPE_NONE, chooseReplyScope(false,
                                                 true,
                                                 true));
}

// second half of the bug: a DIRECT request carries no transport codes, so recv_pkt_region is
// always NULL. Un-scoped is dropped under flood.max.unscoped=0, and floods the mesh otherwise.
TEST(ReplyScope, FallsBackToDefaultScopeWhenRequestScopeUnknown) {
    EXPECT_EQ(REPLY_SCOPE_DEFAULT, chooseReplyScope(false,
                                                    false,
                                                    true));
}

TEST(ReplyScope, SendsUnscopedOnlyWhenNoScopeIsAvailableAtAll) {
    EXPECT_EQ(REPLY_SCOPE_NONE, chooseReplyScope(false, false, false));
    EXPECT_EQ(REPLY_SCOPE_NONE, chooseReplyScope(false, true, false));
}

static Packet makePath(uint8_t hash_size, uint8_t hops) {
    Packet p;
    p.header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT);
    p.setPathHashSizeAndCount(hash_size, hops);
    p.payload_len = 1;
    return p;
}

// the reported bug: with 1-byte hashes the byte limit still allows a 64th hash, but the
// 6-bit count field cannot hold 64. setPathHashCount(64) spilled into the hash-size bits and
// the packet re-flooded as a zero-hop, 2-byte-hash packet with a fresh hop budget.
TEST(AppendPathHash, OneByteHashesStopAt63) {
    auto ok = makePath(1, 62);
    EXPECT_TRUE(canAppendPathHash(&ok));

    auto full = makePath(1, 63);
    EXPECT_FALSE(canAppendPathHash(&full));
}

TEST(AppendPathHash, TwoAndThreeByteHashesStopAtTheByteLimit) {
    auto two_ok = makePath(2, 31);
    EXPECT_TRUE(canAppendPathHash(&two_ok));
    auto two_full = makePath(2, 32);   // 32 * 2 == 64, no room for a 33rd
    EXPECT_FALSE(canAppendPathHash(&two_full));

    auto three_ok = makePath(3, 20);
    EXPECT_TRUE(canAppendPathHash(&three_ok));
    auto three_full = makePath(3, 21);  // 22 * 3 > 64
    EXPECT_FALSE(canAppendPathHash(&three_full));
}

// every append the policy allows must keep the hash size and land on exactly n + 1
TEST(AppendPathHash, AllowedAppendNeverChangesHashSize) {
    for (uint8_t sz = 1; sz <= 3; sz++) {
        for (uint8_t n = 0; n < 64; n++) {
            auto pkt = makePath(sz, n);
            if (!canAppendPathHash(&pkt)) continue;
            pkt.setPathHashCount(n + 1);
            EXPECT_EQ(sz, pkt.getPathHashSize()) << "sz=" << (int)sz << " n=" << (int)n;
            EXPECT_EQ(n + 1, pkt.getPathHashCount()) << "sz=" << (int)sz << " n=" << (int)n;
        }
    }
}

TEST(AppendPathHash, ReservedHashSizeIsNeverExtended) {
    Packet p;
    p.header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT);
    p.path_len = 0xC0 | 1;  // hash size bits == 3 (reserved), count 1
    p.payload_len = 1;
    EXPECT_FALSE(canAppendPathHash(&p));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
