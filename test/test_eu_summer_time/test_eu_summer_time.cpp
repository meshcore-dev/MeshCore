#include <gtest/gtest.h>
#include "helpers/ui/EuSummerTime.h"

TEST(EuSummerTime, StartsLastSundayOfMarchAt1Utc) {
  EXPECT_FALSE(isEuSummerTime(1774745999));  // 2026-03-29 00:59:59 UTC
  EXPECT_TRUE(isEuSummerTime(1774746000));   // 2026-03-29 01:00:00 UTC
  EXPECT_TRUE(isEuSummerTime(1806195600));   // 2027-03-28 01:00:00 UTC
}

TEST(EuSummerTime, EndsLastSundayOfOctoberAt1Utc) {
  EXPECT_TRUE(isEuSummerTime(1792889999));   // 2026-10-25 00:59:59 UTC
  EXPECT_FALSE(isEuSummerTime(1792890000));  // 2026-10-25 01:00:00 UTC
  EXPECT_TRUE(isEuSummerTime(1824944399));   // 2027-10-31 00:59:59 UTC, the 31st is a Sunday
}

TEST(EuSummerTime, WinterAroundNewYear) {
  EXPECT_FALSE(isEuSummerTime(1798761599));  // 2026-12-31 23:59:59 UTC
  EXPECT_FALSE(isEuSummerTime(1798761600));  // 2027-01-01 00:00:00 UTC
}

TEST(ClockDstOffset, OffKeepsTheTime) {
  EXPECT_EQ(0, clockDstOffset(0, 1774746000));
}

TEST(ClockDstOffset, EuAddsOneHourInSummerOnly) {
  EXPECT_EQ(3600, clockDstOffset(1, 1774746000));   // 2026-03-29 01:00:00 UTC
  EXPECT_EQ(0, clockDstOffset(1, 1798761600));      // 2027-01-01 00:00:00 UTC
}

TEST(ClockDstOffset, UnknownRuleKeepsTheTime) {
  EXPECT_EQ(0, clockDstOffset(7, 1774746000));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
