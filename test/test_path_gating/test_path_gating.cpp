/**
 * test_path_gating.cpp
 *
 * Unit tests for the path-stickiness lock introduced in
 * BaseChatMesh::onContactPathRecv (fixes issue #1775).
 *
 * Strategy: blanket first-arrived lock for PATH_STICKINESS_WINDOW_SECS (10 s).
 * ANY replacement of a freshly stored path is blocked during the window,
 * regardless of hop count.  Fewer hops is NOT automatically better:
 * rxdelay-based SNR ordering propagates high-SNR long-hop paths first, so a
 * hop-count preference would bias toward the wrong path.  After the window
 * expires any new path is accepted unconditionally.
 *
 * See docs/reliability_changes.md for the full rationale.
 */

#include <gtest/gtest.h>
#include <stdint.h>

// ---- Constants mirrored from BaseChatMesh.cpp --------------------------------
#define OUT_PATH_UNKNOWN            0xFF
#define PATH_STICKINESS_WINDOW_SECS 10u   // 10 seconds blanket first-arrived lock

// ---- Path-lock decision extracted as a pure function -----------------------
// Must remain an exact transcription of the condition in onContactPathRecv.
static bool shouldKeepStoredPath(
    uint32_t now,
    uint8_t  stored_path_len,
    uint32_t stored_path_timestamp)
{
  if (stored_path_len == OUT_PATH_UNKNOWN) return false;
  if (stored_path_timestamp == 0)          return false;
  uint32_t age = now - stored_path_timestamp;
  return age < PATH_STICKINESS_WINDOW_SECS;  // blanket lock — no hop comparison
}

// =============================================================================
// No stored path — lock must never block
// =============================================================================

TEST(PathGating, NoStoredPath_AlwaysAcceptsIncoming) {
  EXPECT_FALSE(shouldKeepStoredPath(1000, OUT_PATH_UNKNOWN, 0));
  EXPECT_FALSE(shouldKeepStoredPath(1000, OUT_PATH_UNKNOWN, 999));
}

// =============================================================================
// Stale path (age >= window) — lock must not block
// =============================================================================

TEST(PathGating, StaleStoredPath_ExactlyAtWindowBoundary_Accepts) {
  uint32_t now = 1000;
  uint32_t ts  = now - PATH_STICKINESS_WINDOW_SECS;  // age == window → expired
  EXPECT_FALSE(shouldKeepStoredPath(now, 1, ts));
}

TEST(PathGating, StaleStoredPath_JustOutsideWindow_Accepts) {
  uint32_t now = 1000;
  uint32_t ts  = now - (PATH_STICKINESS_WINDOW_SECS + 1);
  EXPECT_FALSE(shouldKeepStoredPath(now, 1, ts));
}

TEST(PathGating, PathExpires_After10Seconds_Accepts) {
  uint32_t now = 500;
  uint32_t ts  = now - PATH_STICKINESS_WINDOW_SECS;
  EXPECT_FALSE(shouldKeepStoredPath(now, 2, ts));
}

TEST(PathGating, VeryOldStoredPath_Accepts) {
  EXPECT_FALSE(shouldKeepStoredPath(10000, 1, 1u));
}

// =============================================================================
// Fresh path (age < window) — ANY replacement is blocked (blanket lock)
// =============================================================================

TEST(PathGating, FreshPath_BlocksReplacement_1SecondOld) {
  uint32_t now = 1000;
  uint32_t ts  = now - 1;
  EXPECT_TRUE(shouldKeepStoredPath(now, 1, ts));
}

TEST(PathGating, FreshPath_BlocksReplacement_5SecondsOld) {
  uint32_t now = 1000;
  uint32_t ts  = now - 5;
  EXPECT_TRUE(shouldKeepStoredPath(now, 1, ts));
}

TEST(PathGating, FreshPath_BlocksReplacement_JustInsideWindow) {
  uint32_t now = 1000;
  uint32_t ts  = now - (PATH_STICKINESS_WINDOW_SECS - 1);  // 1 s before expiry
  EXPECT_TRUE(shouldKeepStoredPath(now, 1, ts));
}

// Key property: blanket lock applies regardless of the incoming hop count.
// These cases verify no hop comparison is performed.

TEST(PathGating, FreshPath_BlocksReplacement_LongerHopIncoming) {
  // stored=1 hop, new=3 hops — old logic would also block; new logic still blocks
  uint32_t now = 1000, ts = now - 1;
  EXPECT_TRUE(shouldKeepStoredPath(now, 1, ts));
}

TEST(PathGating, FreshPath_BlocksReplacement_ShorterHopIncoming) {
  // stored=3 hops, new=1 hop — old logic would accept (fewer hops); new logic blocks
  uint32_t now = 1000, ts = now - 1;
  EXPECT_TRUE(shouldKeepStoredPath(now, 3, ts));
}

TEST(PathGating, FreshPath_BlocksReplacement_SameHopIncoming) {
  // stored=2 hops, new=2 hops — both old and new logic block
  uint32_t now = 1000, ts = now - 1;
  EXPECT_TRUE(shouldKeepStoredPath(now, 2, ts));
}

// =============================================================================
// Zero timestamp — treated as "never set", must never lock
// =============================================================================

TEST(PathGating, ZeroTimestamp_AlwaysAcceptsNew) {
  EXPECT_FALSE(shouldKeepStoredPath(1000, 2, 0));
  EXPECT_FALSE(shouldKeepStoredPath(1000, 1, 0));
}

// =============================================================================
// Window constant sanity
// =============================================================================

TEST(PathGating, WindowIs10Seconds) {
  EXPECT_EQ(10u, PATH_STICKINESS_WINDOW_SECS);
}


int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
