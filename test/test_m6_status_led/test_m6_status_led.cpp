#include <gtest/gtest.h>
#include "../../variants/thinknode_m6/StatusLed.h"

TEST(M6RedLed, OnForTenSecondsAtBoot) {
  EXPECT_TRUE(m6RedLed(4000, false, 3900));
  EXPECT_FALSE(m6RedLed(10500, false, 3900));
}

TEST(M6RedLed, SteadyWhileCharging) {
  EXPECT_TRUE(m6RedLed(20000, true, 3900));
  EXPECT_TRUE(m6RedLed(20600, true, 3400));
}

TEST(M6RedLed, ShortFlashEveryThreeSecondsOnAGoodBattery) {
  EXPECT_TRUE(m6RedLed(21000, false, 3900));
  EXPECT_TRUE(m6RedLed(21001, false, 3900));
  EXPECT_FALSE(m6RedLed(21002, false, 3900));
  EXPECT_FALSE(m6RedLed(22500, false, 3900));
  EXPECT_TRUE(m6RedLed(24000, false, 0));   // bad reading, not a low battery
}

TEST(M6RedLed, SlowFlashingOnALowBattery) {
  EXPECT_TRUE(m6RedLed(20000, false, 3450));
  EXPECT_FALSE(m6RedLed(20600, false, 3450));
  EXPECT_TRUE(m6RedLed(25100, false, 3500));
}

TEST(M6BlueLed, FlashesAtFiveHertzDuringData) {
  EXPECT_TRUE(m6BlueLed(true, 0));
  EXPECT_TRUE(m6BlueLed(true, 99));
  EXPECT_FALSE(m6BlueLed(true, 100));
  EXPECT_TRUE(m6BlueLed(true, 200));
  EXPECT_FALSE(m6BlueLed(false, 0));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
