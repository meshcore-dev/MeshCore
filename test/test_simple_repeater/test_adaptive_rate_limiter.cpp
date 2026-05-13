#include <stdint.h>

#include <gtest/gtest.h>

#include "RateLimiter.h"

static int drainWindow(AdaptiveRateLimiter& limiter, uint32_t now) {
  int allowed = 0;
  while (limiter.allow(now) && allowed < 255) {
    ++allowed;
  }
  return allowed;
}

TEST(AdaptiveRateLimiter, UsesFloorInInitialWindow) {
  AdaptiveRateLimiter limiter(300, 3, 5);
  EXPECT_EQ(5, drainWindow(limiter, 1));
}

TEST(AdaptiveRateLimiter, RollsOverAtExactWindowBoundary) {
  AdaptiveRateLimiter limiter(10, 1, 2);

  EXPECT_EQ(2, drainWindow(limiter, 1));
  EXPECT_EQ(0, drainWindow(limiter, 9));
  EXPECT_EQ(2, drainWindow(limiter, 10));
}

TEST(AdaptiveRateLimiter, CountsTrafficSpreadAcrossSameWindow) {
  AdaptiveRateLimiter limiter(10, 3, 2);

  EXPECT_TRUE(limiter.allow(1));
  EXPECT_TRUE(limiter.allow(5));
  EXPECT_FALSE(limiter.allow(9));
  EXPECT_EQ(6, drainWindow(limiter, 10));
}

TEST(AdaptiveRateLimiter, GrowsLimitWithSustainedLoad) {
  AdaptiveRateLimiter limiter(10, 3, 2);

  const int initialWindow = drainWindow(limiter, 1);
  const int secondWindow = drainWindow(limiter, 10);
  const int thirdWindow = drainWindow(limiter, 20);

  EXPECT_EQ(2, initialWindow);
  EXPECT_EQ(6, secondWindow);
  EXPECT_EQ(9, thirdWindow);
  EXPECT_GT(secondWindow, initialWindow);
  EXPECT_GT(thirdWindow, secondWindow);
}

TEST(AdaptiveRateLimiter, DecaysBackToFloorAfterLongIdle) {
  AdaptiveRateLimiter limiter(10, 3, 2);

  EXPECT_EQ(2, drainWindow(limiter, 1));
  EXPECT_EQ(6, drainWindow(limiter, 10));
  EXPECT_EQ(9, drainWindow(limiter, 20));
  EXPECT_EQ(2, drainWindow(limiter, 3000));
}

TEST(AdaptiveRateLimiter, LargeTimeJumpAgesAcrossMultipleWindows) {
  AdaptiveRateLimiter shortJump(10, 3, 2);
  AdaptiveRateLimiter longJump(10, 3, 2);

  EXPECT_EQ(2, drainWindow(shortJump, 1));
  EXPECT_EQ(6, drainWindow(shortJump, 10));
  EXPECT_EQ(9, drainWindow(shortJump, 20));

  EXPECT_EQ(2, drainWindow(longJump, 1));
  EXPECT_EQ(6, drainWindow(longJump, 10));
  EXPECT_EQ(9, drainWindow(longJump, 20));

  const int nextWindowShort = drainWindow(shortJump, 30);
  const int afterLongIdle = drainWindow(longJump, 3000);

  EXPECT_GT(nextWindowShort, afterLongIdle);
  EXPECT_EQ(2, afterLongIdle);
}

TEST(AdaptiveRateLimiter, HandlesUint32TimestampWraparound) {
  AdaptiveRateLimiter limiter(10, 3, 2);
  const uint32_t beforeWrap = UINT32_MAX - 4;

  EXPECT_EQ(2, drainWindow(limiter, beforeWrap));
  EXPECT_EQ(0, drainWindow(limiter, 4));
  EXPECT_EQ(3, drainWindow(limiter, 5));
}

TEST(AdaptiveRateLimiter, ClampsLimitToUint8Maximum) {
  AdaptiveRateLimiter limiter(10, 255, 255);

  EXPECT_EQ(255, drainWindow(limiter, 1));
  EXPECT_EQ(255, drainWindow(limiter, 10));
}

TEST(AdaptiveRateLimiter, StatsInitialState) {
  AdaptiveRateLimiter limiter(10, 3, 5);
  const AdaptiveRateLimiterStats stats = limiter.stats(1);

  EXPECT_EQ(5, stats.limit);
  EXPECT_EQ(5, stats.remaining);
  EXPECT_EQ(0, stats.denied);
  EXPECT_EQ(5, stats.load_avg);
  EXPECT_EQ(0, stats.limit_reached_at);
}

TEST(AdaptiveRateLimiter, StatsRemainingDecreasesOnAllow) {
  AdaptiveRateLimiter limiter(10, 3, 5);

  EXPECT_TRUE(limiter.allow(1));
  EXPECT_TRUE(limiter.allow(1));
  const AdaptiveRateLimiterStats stats = limiter.stats(1);

  EXPECT_EQ(3, stats.remaining);
  EXPECT_EQ(0, stats.denied);
}

TEST(AdaptiveRateLimiter, StatsRecordsDeniedAndLimitReachedAt) {
  AdaptiveRateLimiter limiter(10, 3, 2);

  EXPECT_TRUE(limiter.allow(1));
  EXPECT_TRUE(limiter.allow(1));
  EXPECT_FALSE(limiter.allow(2));
  EXPECT_FALSE(limiter.allow(3));

  const AdaptiveRateLimiterStats stats = limiter.stats(3);

  EXPECT_EQ(0, stats.remaining);
  EXPECT_EQ(2, stats.denied);
  EXPECT_EQ(1, stats.limit_reached_at);
}

TEST(AdaptiveRateLimiter, StatsDeniedResetsOnWindowRollover) {
  AdaptiveRateLimiter limiter(10, 3, 2);

  EXPECT_TRUE(limiter.allow(1));
  EXPECT_TRUE(limiter.allow(1));
  EXPECT_FALSE(limiter.allow(2));

  const AdaptiveRateLimiterStats stats = limiter.stats(10);

  EXPECT_EQ(0, stats.denied);
  EXPECT_EQ(1, stats.limit_reached_at);
}

TEST(AdaptiveRateLimiter, StatsDeniedSaturatesAt255) {
  AdaptiveRateLimiter limiter(10, 3, 1);

  EXPECT_TRUE(limiter.allow(1));

  for (int i = 0; i < 300; ++i)
    EXPECT_FALSE(limiter.allow(1));

  const AdaptiveRateLimiterStats stats = limiter.stats(1);

  EXPECT_EQ(255, stats.denied);
}

TEST(AdaptiveRateLimiter, ClearStatsOnlyClearsReportingCounters) {
  AdaptiveRateLimiter limiter(10, 3, 2);

  EXPECT_TRUE(limiter.allow(1));
  EXPECT_TRUE(limiter.allow(1));
  EXPECT_FALSE(limiter.allow(2));

  const AdaptiveRateLimiterStats beforeClear = limiter.stats(2);

  EXPECT_EQ(0, beforeClear.remaining);
  EXPECT_EQ(1, beforeClear.denied);
  EXPECT_EQ(1, beforeClear.limit_reached_at);

  limiter.clearStats();
  const AdaptiveRateLimiterStats afterClear = limiter.stats(2);

  EXPECT_EQ(2, afterClear.limit);
  EXPECT_EQ(0, afterClear.remaining);
  EXPECT_EQ(0, afterClear.denied);
  EXPECT_EQ(2, afterClear.load_avg);
  EXPECT_EQ(0, afterClear.limit_reached_at);
  EXPECT_FALSE(limiter.allow(2));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
