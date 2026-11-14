#include <gtest/gtest.h>

#include <helpers/RTCValidity.h>

TEST(RTCValidity, SeedsLostPowerClockFromFallback) {
  uint32_t adjusted_to = 0;

  bool seeded = mesh::seedRTCIfLostPower(true, 1715770351,
                                         [&](uint32_t time) { adjusted_to = time; });

  EXPECT_TRUE(seeded);
  EXPECT_EQ(1715770351u, adjusted_to);
}

TEST(RTCValidity, PreservesValidBatteryBackedClock) {
  bool adjusted = false;

  bool seeded = mesh::seedRTCIfLostPower(false, 1715770351,
                                         [&](uint32_t) { adjusted = true; });

  EXPECT_FALSE(seeded);
  EXPECT_FALSE(adjusted);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
