#include "helpers/asr650x/Button.h"

#include <cstdio>
#include <gtest/gtest.h>

TEST(Asr650xButton, Behaves) {
  /* a clean press is reported once, after the debounce time, and not again while held */
  {
    asr650x::Button b;
    EXPECT_TRUE(!b.update(false, 0));
    EXPECT_TRUE(!b.update(true, 100));
    EXPECT_TRUE(!b.update(true, 120)); // 20 ms: still bouncing
    EXPECT_TRUE(b.update(true, 135));  // >= 30 ms held: reported
    EXPECT_TRUE(!b.update(true, 500)); // held: no repeat
    EXPECT_TRUE(!b.update(false, 600));
    EXPECT_TRUE(!b.update(true, 700));
    EXPECT_TRUE(b.update(true, 731)); // second press needs the release in between
  }
  /* contact bounce shorter than the debounce time never fires */
  {
    asr650x::Button b;
    EXPECT_TRUE(!b.update(true, 0));
    EXPECT_TRUE(!b.update(false, 10));
    EXPECT_TRUE(!b.update(true, 20));
    EXPECT_TRUE(!b.update(false, 40));
    EXPECT_TRUE(!b.update(false, 1000));
  }
  /* readings during the hold-off after a battery measurement are ignored, even a long LOW level */
  {
    asr650x::Button b;
    b.holdoff(1000, 50);
    EXPECT_TRUE(!b.update(true, 1001));
    EXPECT_TRUE(!b.update(true, 1030));
    EXPECT_TRUE(!b.update(true, 1049));
    EXPECT_TRUE(!b.update(
        true, 1060)); // pin still LOW after the hold-off: not a new press, it must be released first
    EXPECT_TRUE(!b.update(false, 1100));
    EXPECT_TRUE(!b.update(true, 1200));
    EXPECT_TRUE(b.update(true, 1235));
  }
  /* a press that began before the hold-off and is still held after it must not fire */
  {
    asr650x::Button b;
    EXPECT_TRUE(!b.update(true, 0));
    b.holdoff(10, 50);
    EXPECT_TRUE(!b.update(true, 100));
    EXPECT_TRUE(!b.update(true, 200));
  }
  /* millis() wrap-around */
  {
    asr650x::Button b;
    EXPECT_TRUE(!b.update(true, 0xFFFFFFF0u));
    EXPECT_TRUE(b.update(true, 0x00000020u)); // 48 ms later across the wrap
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
