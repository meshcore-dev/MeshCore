#include <gtest/gtest.h>

#include "../../examples/simple_repeater/RepeaterTimeSyncBuildDefaults.h"

TEST(RepeaterTimeSyncBuildDefaults, PreserveOptInUpstreamDefaults) {
  EXPECT_STREQ("", repeater_time_sync_build_defaults::CHANNEL);
  EXPECT_STREQ("", repeater_time_sync_build_defaults::DISPLAY_NAME);
  EXPECT_STREQ("", repeater_time_sync_build_defaults::PUBLIC_KEY);
  EXPECT_EQ(0, repeater_time_sync_build_defaults::ENABLED);
}

TEST(RepeaterTimeSyncBuildDefaults, ValidateSupportedChannelBoundaries) {
  EXPECT_TRUE(repeater_time_sync_build_defaults::isSupportedChannel("public"));
  EXPECT_TRUE(repeater_time_sync_build_defaults::isSupportedChannel("#t"));
  EXPECT_FALSE(repeater_time_sync_build_defaults::isSupportedChannel(""));
  EXPECT_FALSE(repeater_time_sync_build_defaults::isSupportedChannel("time"));
  EXPECT_FALSE(repeater_time_sync_build_defaults::isSupportedChannel("#"));
}

TEST(RepeaterTimeSyncBuildDefaults, RejectUnsafeDisplayNames) {
  EXPECT_TRUE(repeater_time_sync_build_defaults::isSafeDisplayName("SquirrelBot"));
  EXPECT_FALSE(repeater_time_sync_build_defaults::isSafeDisplayName(""));
  EXPECT_FALSE(repeater_time_sync_build_defaults::isSafeDisplayName("bad\nname"));
  EXPECT_FALSE(repeater_time_sync_build_defaults::isSafeDisplayName("123456789012345678901"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
