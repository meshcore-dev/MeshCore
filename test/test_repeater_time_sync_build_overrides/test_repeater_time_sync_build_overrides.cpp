#include <gtest/gtest.h>

// Model the ignored PlatformIO values used for the private repeater fleet.
#define REPEATER_DEFAULT_TIME_SYNC_CHANNEL "#time"
#define REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME "SquirrelBot"
#define REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY "12e621a0005eb20955c82f8a971a81a7504e080803aed50591f8c703b4219d5e"
#define REPEATER_DEFAULT_TIME_SYNC_ENABLED 1

#include "../../examples/simple_repeater/RepeaterTimeSyncBuildDefaults.h"

TEST(RepeaterTimeSyncBuildDefaults, AcceptPrivateDeploymentOverrides) {
  EXPECT_STREQ("#time", repeater_time_sync_build_defaults::CHANNEL);
  EXPECT_STREQ("SquirrelBot", repeater_time_sync_build_defaults::DISPLAY_NAME);
  EXPECT_STREQ("12e621a0005eb20955c82f8a971a81a7504e080803aed50591f8c703b4219d5e",
               repeater_time_sync_build_defaults::PUBLIC_KEY);
  EXPECT_EQ(1, repeater_time_sync_build_defaults::ENABLED);
  EXPECT_TRUE(repeater_time_sync_build_defaults::isHexPublicKey(
    repeater_time_sync_build_defaults::PUBLIC_KEY));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
