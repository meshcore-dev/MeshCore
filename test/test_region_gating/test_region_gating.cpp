#include <gtest/gtest.h>
#include "helpers/RegionMap.h"

// Unit tests for duty-cycle region gating (issue #2747).
// These exercise the pure hierarchy/level logic; no crypto or filesystem is touched.

static bool gated(RegionMap& rm, const char* name) {
  RegionEntry* r = rm.findByName(name);   // findByName("*") returns the wildcard
  return r && (r->rt_flags & REGION_DENY_FLOOD);
}

// Build:  * > eu > nl > nl-ge > nl-ge-nij   (a single deep chain)
static void buildNlChain(RegionMap& rm) {
  RegionEntry* eu   = rm.putRegion("eu", 0);
  RegionEntry* nl   = rm.putRegion("nl", eu->id);
  RegionEntry* nlge = rm.putRegion("nl-ge", nl->id);
  rm.putRegion("nl-ge-nij", nlge->id);
}

// Build:  * > cz > { cz-ulk, cz-stc, cz-lbk }   (three regions on the same level)
static void buildCzFlat(RegionMap& rm) {
  RegionEntry* cz = rm.putRegion("cz", 0);
  rm.putRegion("cz-ulk", cz->id);
  rm.putRegion("cz-stc", cz->id);
  rm.putRegion("cz-lbk", cz->id);
}

TEST(RegionGating, DepthAndMaxGateLevel) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  buildNlChain(rm);
  EXPECT_EQ(rm.getMaxDepth(), 4);
  EXPECT_EQ(rm.getMaxGateLevel(), 4);
}

TEST(RegionGating, GatesWildcardFirstThenOutermostInward) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  buildNlChain(rm);

  rm.applyDutyGate(0);
  EXPECT_FALSE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "eu"));

  rm.applyDutyGate(1);   // wildcard first
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "eu"));

  rm.applyDutyGate(2);   // + broadest named region
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_FALSE(gated(rm, "nl"));

  rm.applyDutyGate(3);
  EXPECT_TRUE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));

  rm.applyDutyGate(4);   // max: everything except the innermost cluster
  EXPECT_TRUE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));   // innermost local cluster is always protected
}

TEST(RegionGating, InnermostClusterNeverGatedEvenAboveMax) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  buildNlChain(rm);
  rm.applyDutyGate(99);   // clamped behaviour
  EXPECT_TRUE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST(RegionGating, RecoveryReenablesInsideOut) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  buildNlChain(rm);

  rm.applyDutyGate(4);
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_TRUE(gated(rm, "nl-ge"));

  rm.applyDutyGate(2);   // step back down: nl and nl-ge come back first
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_FALSE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));

  rm.applyDutyGate(0);   // fully recovered
  EXPECT_FALSE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "eu"));
}

TEST(RegionGating, MultipleRegionsOnSameLevelGateTogetherAndLeavesProtected) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  buildCzFlat(rm);
  EXPECT_EQ(rm.getMaxDepth(), 2);
  EXPECT_EQ(rm.getMaxGateLevel(), 2);

  rm.applyDutyGate(1);   // wildcard only
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "cz"));

  rm.applyDutyGate(2);   // wildcard + cz; the three leaf regions stay up
  EXPECT_TRUE(gated(rm, "cz"));
  EXPECT_FALSE(gated(rm, "cz-ulk"));
  EXPECT_FALSE(gated(rm, "cz-stc"));
  EXPECT_FALSE(gated(rm, "cz-lbk"));
}

TEST(RegionGating, LoneWildcardIsNeverGated) {
  TransportKeyStore ks;
  RegionMap rm(ks);   // no named regions
  EXPECT_EQ(rm.getMaxDepth(), 0);
  EXPECT_EQ(rm.getMaxGateLevel(), 0);
  rm.applyDutyGate(1);
  EXPECT_FALSE(gated(rm, "*"));   // wildcard is the local cluster here, keep serving
}

TEST(RegionGating, SingleNamedRegionGatesWildcardButKeepsRegion) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  rm.putRegion("cz", 0);
  EXPECT_EQ(rm.getMaxGateLevel(), 1);
  rm.applyDutyGate(1);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "cz"));
}

TEST(RegionGating, RuntimeGateIsSeparateFromConfigFlags) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  buildNlChain(rm);

  RegionEntry* eu = rm.findByName("eu");
  eu->flags &= (uint8_t)~REGION_DENY_FLOOD;   // admin: this region allows flood

  rm.applyDutyGate(2);                          // transiently gate 'eu'
  EXPECT_TRUE(eu->rt_flags & REGION_DENY_FLOOD);
  EXPECT_FALSE(eu->flags & REGION_DENY_FLOOD);  // config untouched
  EXPECT_TRUE(eu->effectiveFlags() & REGION_DENY_FLOOD);

  rm.applyDutyGate(0);                          // recover
  EXPECT_FALSE(eu->rt_flags & REGION_DENY_FLOOD);
  EXPECT_FALSE(eu->effectiveFlags() & REGION_DENY_FLOOD);   // flood allowed again
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
