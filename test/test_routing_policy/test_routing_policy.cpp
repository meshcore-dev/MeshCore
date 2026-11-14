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

TEST(FloodMaxTypes, EmptyListChangesNothing) {
    auto unscoped = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&unscoped, 64, 0, 8, ""));
    EXPECT_TRUE(isFloodHopLimitExceeded(&unscoped, 64, 0, 8, NULL));

    auto scoped = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, 3);
    EXPECT_FALSE(isFloodHopLimitExceeded(&scoped, 64, 0, 8, ""));
    EXPECT_FALSE(isFloodHopLimitExceeded(&scoped, 64, 0, 8, NULL));

    auto advert = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 8);
    EXPECT_TRUE(isFloodHopLimitExceeded(&advert, 64, 64, 8, ""));

    auto capped = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, 5);
    EXPECT_TRUE(isFloodHopLimitExceeded(&capped, 5, 64, 8, ""));
}

// the default on repeaters and room servers: req and response stop at 5 hops, other types do not
TEST(FloodMaxTypes, DefaultLimitsReqAndResponseOnly) {
    const char* dflt = "0:5,1:5";
    auto req_near = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_REQ, 4);
    EXPECT_FALSE(isFloodHopLimitExceeded(&req_near, 64, 64, 8, dflt));
    auto req_far = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_REQ, 5);
    EXPECT_TRUE(isFloodHopLimitExceeded(&req_far, 64, 64, 8, dflt));
    auto resp_far = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 5);
    EXPECT_TRUE(isFloodHopLimitExceeded(&resp_far, 64, 64, 8, dflt));
    auto grptxt_far = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_GRP_TXT, 20);
    EXPECT_FALSE(isFloodHopLimitExceeded(&grptxt_far, 64, 64, 8, dflt));
}

// every limit that applies is tested, so the strictest wins: the same rule flood.max and
// flood.max.unscoped have always followed
TEST(FloodMaxTypes, StrictestLimitWins) {
    // list is stricter than flood.max
    auto a = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG, 2);
    EXPECT_TRUE(isFloodHopLimitExceeded(&a, 64, 64, 8, "2:2"));
    // flood.max is stricter than the list
    auto b = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG, 2);
    EXPECT_TRUE(isFloodHopLimitExceeded(&b, 2, 64, 8, "2:30"));
    // an advert entry joins flood.max.advert on the same terms: 3 wins over 8
    auto adv = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 3);
    EXPECT_TRUE(isFloodHopLimitExceeded(&adv, 64, 64, 8, "4:3"));
    // and 8 wins over 30
    auto adv2 = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 8);
    EXPECT_TRUE(isFloodHopLimitExceeded(&adv2, 64, 64, 8, "4:30"));
}

// a damaged list keeps what was read before the damage and drops the rest; it never applies a
// value it did not read, and it never runs off the end of the buffer
TEST(FloodMaxTypes, MalformedListStopsAtTheDamage) {
    EXPECT_EQ(4, floodMaxForType("6:4,xx", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:4,9", PAYLOAD_TYPE_TRACE));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType(":", PAYLOAD_TYPE_REQ));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType(",,,", PAYLOAD_TYPE_REQ));
    // out of range entries are skipped, not clamped
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:65", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("99:4", PAYLOAD_TYPE_GRP_DATA));
    // and a skipped entry does not stop the ones behind it
    EXPECT_EQ(3, floodMaxForType("6:65,9:3", PAYLOAD_TYPE_TRACE));
}

TEST(FloodMaxTypes, SetInsertsChangesAndRemovesSortedByType) {
    char list[FLOOD_MAX_TYPES_LEN] = "";
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_TRACE, 3));
    EXPECT_STREQ("9:3", list);
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_GRP_DATA, 4));
    EXPECT_STREQ("6:4,9:3", list);                       // sorted by type, not by order of setting
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_GRP_DATA, 10));
    EXPECT_STREQ("6:10,9:3", list);                      // changed, not duplicated
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_GRP_DATA, FLOOD_MAX_TYPE_NONE));
    EXPECT_STREQ("9:3", list);                           // removed
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_TRACE, FLOOD_MAX_TYPE_NONE));
    EXPECT_STREQ("", list);                              // back to empty, ie. back to today's behaviour
}

// all 16 slots filled is the worst case the buffer has to hold
TEST(FloodMaxTypes, BufferHoldsEverySlotAtItsLongest) {
    char list[FLOOD_MAX_TYPES_LEN] = "";
    for (uint8_t t = 0; t < FLOOD_MAX_TYPE_SLOTS; t++) {
        EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), t, 64)) << "type=" << (int)t;
    }
    EXPECT_STREQ("0:64,1:64,2:64,3:64,4:64,5:64,6:64,7:64,8:64,9:64,10:64,11:64,12:64,13:64,14:64,15:64", list);
    EXPECT_EQ(85u, strlen(list));   // the worst case the buffer has to hold
    EXPECT_LT(strlen(list), sizeof(list));
    for (uint8_t t = 0; t < FLOOD_MAX_TYPE_SLOTS; t++) EXPECT_EQ(64, floodMaxForType(list, t));
}

// ---- what "set flood.max.type ..." and "get flood.max.type" do with their strings ----

TEST(FloodMaxTypeCmd, ReadsTypeAndHops) {
    uint8_t type = 99, hops = 99;
    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("grpdata 4", &type, &hops));
    EXPECT_EQ(PAYLOAD_TYPE_GRP_DATA, type);
    EXPECT_EQ(4, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("9 0", &type, &hops));
    EXPECT_EQ(PAYLOAD_TYPE_TRACE, type);
    EXPECT_EQ(0, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("trace 64", &type, &hops));
    EXPECT_EQ(64, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("trace off", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("  grpdata   4", &type, &hops));   // extra spaces
    EXPECT_EQ(PAYLOAD_TYPE_GRP_DATA, type);
    EXPECT_EQ(4, hops);
}

TEST(FloodMaxTypeCmd, RefusesBadArgumentsWithoutTouchingTheOutputs) {
    uint8_t type = 7, hops = 7;
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_TYPE, parseFloodMaxTypeCmd("nosuchtype 4", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_TYPE, parseFloodMaxTypeCmd("", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_TYPE, parseFloodMaxTypeCmd("16 4", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace 65", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace -1", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace four", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace 4x", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace offf", &type, &hops));
    EXPECT_EQ(7, type);
    EXPECT_EQ(7, hops);
}

// the answer "get flood.max.type" gives, in the 160 byte reply buffer the CLI uses
TEST(FloodMaxTypeCmd, PrintsTheListTheWayGetAnswersIt) {
    char reply[160];
    formatFloodMaxTypes("", reply, sizeof(reply));
    EXPECT_STREQ("> none", reply);
    formatFloodMaxTypes(NULL, reply, sizeof(reply));
    EXPECT_STREQ("> none", reply);

    formatFloodMaxTypes("6:4", reply, sizeof(reply));
    EXPECT_STREQ("> grpdata:4", reply);

    formatFloodMaxTypes("2:10,6:4,9:3", reply, sizeof(reply));
    EXPECT_STREQ("> txt:10 grpdata:4 trace:3", reply);

    formatFloodMaxTypes("12:5", reply, sizeof(reply));   // a type with no name prints its number
    EXPECT_STREQ("> 12:5", reply);
}


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
