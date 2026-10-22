#include "helpers/asr650x/EpochClock.h"

#include <cstdio>
#include <gtest/gtest.h>

TEST(Asr650xEpochClock, Behaves) {
  asr650x::EpochClock c(1000);
  EXPECT_TRUE(c.now(0) == 1000);   // starts at the boot epoch
  EXPECT_TRUE(c.now(999) == 1000); // not yet a full second
  EXPECT_TRUE(c.now(1000) == 1001);
  EXPECT_TRUE(c.now(3500) == 1003); // remainder carried, not lost
  c.set(5000, 3500);
  EXPECT_TRUE(c.now(3500) == 5000); // set() is exact at that instant
  EXPECT_TRUE(c.now(4499) == 5000);
  EXPECT_TRUE(c.now(4500) == 5001);
  c.set(2000, 4500); // going backwards is allowed
  EXPECT_TRUE(c.now(5500) == 2001);
  asr650x::EpochClock w(100); // millis() wrap at 2^32
  EXPECT_TRUE(w.now(0xFFFFFF00u) == 100 + 4294967u);
  uint32_t before = w.now(0xFFFFFF00u);
  uint32_t after = w.now(0x00000100u); // 0x200 ms later
  EXPECT_TRUE(after >= before && after - before <= 1);
  {
    asr650x::EpochClock s(1000);
    EXPECT_TRUE(!s.synced());
    s.set(2000, 0);
    EXPECT_TRUE(s.synced());
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
