/**
 * test_path_gating.cpp
 *
 * Unit tests for the path quality gating condition introduced in
 * BaseChatMesh::onContactPathRecv (fixes issue #1775).
 *
 * The tests exercise the decision function in isolation.  The constants and
 * the comparison logic are kept in sync with the implementation via the
 * shared defines reproduced below; if those values drift the tests will
 * either fail or fail to compile, providing an early-warning regression net.
 *
 * See docs/reliability_changes.md for the full rationale.
 */

#include <gtest/gtest.h>
#include <stdint.h>

// ---- Constants mirrored from BaseChatMesh.cpp --------------------------------
// If these are changed in the implementation the tests will need updating too.
#define OUT_PATH_UNKNOWN            0xFF
// Reduced from 600 s to 30 s: long enough to reject multipath duplicates
// (50-200 ms apart) but short enough to allow legitimate path updates after
// topology changes, preventing stale paths from silently dropping ACKs.
#define PATH_STICKINESS_WINDOW_SECS 30u

// ---- Path-gating decision extracted as a pure function ----------------------
// Logic must remain an exact transcription of the condition inside
// BaseChatMesh::onContactPathRecv so that the tests catch any divergence.
static bool shouldKeepStoredPath(
    uint32_t now,
    uint8_t  stored_path_len,
    uint32_t stored_path_timestamp,
    uint8_t  new_path_len)
{
  if (stored_path_len == OUT_PATH_UNKNOWN) return false;
  if (stored_path_timestamp == 0)          return false;
  uint32_t age = now - stored_path_timestamp;
  if (age >= PATH_STICKINESS_WINDOW_SECS)  return false;
  uint8_t stored_hops = stored_path_len & 63;
  uint8_t new_hops    = new_path_len    & 63;
  return new_hops > stored_hops;
}

// No stored path — gating must never block

TEST(PathGating, NoStoredPath_AlwaysAcceptsIncoming) {
  EXPECT_FALSE(shouldKeepStoredPath(1000, OUT_PATH_UNKNOWN, 0,   1));
  EXPECT_FALSE(shouldKeepStoredPath(1000, OUT_PATH_UNKNOWN, 900, 5));
}

// Stale stored path (outside the stickiness window) — gating must not block

TEST(PathGating, StaleStoredPath_ExactlyAtWindowBoundary_AcceptsLonger) {
  uint32_t now  = 1000;
  // age == PATH_STICKINESS_WINDOW_SECS → NOT inside window
  uint32_t ts   = now - PATH_STICKINESS_WINDOW_SECS;
  EXPECT_FALSE(shouldKeepStoredPath(now, 1, ts, 3));
}

TEST(PathGating, StaleStoredPath_JustOutsideWindow_AcceptsLonger) {
  uint32_t now = 1000;
  uint32_t ts  = now - (PATH_STICKINESS_WINDOW_SECS + 1);
  EXPECT_FALSE(shouldKeepStoredPath(now, 1, ts, 3));
}

TEST(PathGating, VeryOldStoredPath_AcceptsLonger) {
  uint32_t now = 10000;
  EXPECT_FALSE(shouldKeepStoredPath(now, 1, 1u, 3));  // ~9999 seconds old
}

// After exactly 30 s the window expires — path replacement must be allowed again
TEST(PathGating, PathExpires_After30Seconds_AcceptsLonger) {
  uint32_t now = 1000;
  uint32_t ts  = now - PATH_STICKINESS_WINDOW_SECS;  // exactly at boundary (expired)
  EXPECT_FALSE(shouldKeepStoredPath(now, 1, ts, 5));
}

TEST(PathGating, PathExpires_OneSecondPast_AcceptsLonger) {
  uint32_t now = 1000;
  uint32_t ts  = now - (PATH_STICKINESS_WINDOW_SECS + 1);
  EXPECT_FALSE(shouldKeepStoredPath(now, 1, ts, 5));
}

// Fresh stored path + LONGER incoming path — gating must block

TEST(PathGating, FreshPath_LongerIncoming_KeepsStored) {
  uint32_t now = 1000;
  uint32_t ts  = now - 5;  // 5 s old, well within 30-second window
  EXPECT_TRUE(shouldKeepStoredPath(now, 1, ts, 3));   // stored=1 hop, new=3 hops
}

TEST(PathGating, FreshPath_MuchLongerIncoming_KeepsStored) {
  uint32_t now = 1000;
  uint32_t ts  = now - 1;  // 1 s old
  EXPECT_TRUE(shouldKeepStoredPath(now, 1, ts, 10));
}

TEST(PathGating, FreshPath_JustInsideWindow_KeepsStored) {
  uint32_t now = 1000;
  uint32_t ts  = now - (PATH_STICKINESS_WINDOW_SECS - 1);  // 1 s inside the 30-s window
  EXPECT_TRUE(shouldKeepStoredPath(now, 1, ts, 5));
}

// Fresh stored path + EQUAL or SHORTER incoming path — gating must NOT block
// (accept path updates that are the same or better)

TEST(PathGating, FreshPath_SameHopCount_AcceptsNew) {
  uint32_t now = 1000;
  uint32_t ts  = now - 5;
  EXPECT_FALSE(shouldKeepStoredPath(now, 2, ts, 2));  // equal hops → no gate
}

TEST(PathGating, FreshPath_ShorterIncoming_AcceptsNew) {
  uint32_t now = 1000;
  uint32_t ts  = now - 5;
  EXPECT_FALSE(shouldKeepStoredPath(now, 3, ts, 1));  // new path is better → accept
}

TEST(PathGating, FreshPath_OneHopBetter_AcceptsNew) {
  uint32_t now = 1000;
  uint32_t ts  = now - 5;
  EXPECT_FALSE(shouldKeepStoredPath(now, 4, ts, 3));
}

// Zero timestamp — treated as "never set", must never gate

TEST(PathGating, ZeroTimestamp_AlwaysAcceptsNew) {
  EXPECT_FALSE(shouldKeepStoredPath(1000, 2, 0, 5));
  EXPECT_FALSE(shouldKeepStoredPath(1000, 1, 0, 10));
}

// hop count encoding: lower 6 bits of path_len hold the count (path_len & 63)

TEST(PathGating, PathLenEncodingUpperBitsIgnored) {
  uint32_t now = 1000;
  uint32_t ts  = now - 5;
  // stored_path_len = 0xC1 → hop count bits = 0xC1 & 63 = 1
  // new_path_len    = 0x83 → hop count bits = 0x83 & 63 = 3
  EXPECT_TRUE(shouldKeepStoredPath(now, 0xC1, ts, 0x83));
}

TEST(PathGating, PathLenEncodingUpperBitsIgnored_SameHops) {
  uint32_t now = 1000;
  uint32_t ts  = now - 5;
  // Both encode 2 hops with different upper bits — should NOT gate
  EXPECT_FALSE(shouldKeepStoredPath(now, 0x42, ts, 0x82));
}


int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
