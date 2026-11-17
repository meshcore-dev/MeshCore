#include <gtest/gtest.h>
#include "helpers/ui/ClockBattery.h"

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

TEST(LipoPercent, MatchesTheAppCurve) {
  EXPECT_EQ(0, lipoPercent(3000));
  EXPECT_EQ(0, lipoPercent(3270));
  EXPECT_EQ(40, lipoPercent(3800));
  EXPECT_EQ(43, lipoPercent(3810));  // halfway between 40 and 45, rounded up like the app
  EXPECT_EQ(95, lipoPercent(4150));
  EXPECT_EQ(100, lipoPercent(4200));
  EXPECT_EQ(100, lipoPercent(4363));  // on USB power
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
