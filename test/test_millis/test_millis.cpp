#include <gtest/gtest.h>
#include <helpers/ArduinoTimer.h>
#include <stdint.h>

static constexpr uint32_t MAX = UINT32_MAX;

TEST(MillisHasPassed, ReturnsTrueWhenNowExceedsTarget) {
    EXPECT_TRUE(millisHasPassed(1000, 500));
}

TEST(MillisHasPassed, ReturnsTrueWhenNowEqualsTarget) {
    EXPECT_TRUE(millisHasPassed(1000, 1000));
}

TEST(MillisHasPassed, ReturnsFalseWhenNowIsBeforeTarget) {
    EXPECT_FALSE(millisHasPassed(500, 1000));
}

TEST(MillisHasPassed, ReturnsTrueAfterRollover) {
    // Target was set when millis was near MAX: next_update = (MAX - 500) + 1000
    // which wraps to 499. Now millis has rolled over to 500.
    uint32_t target = MAX - 500 + 1000;  // == 499 via wrapping
    EXPECT_TRUE(millisHasPassed(500, target));
}

TEST(MillisHasPassed, ReturnsFalseJustBeforeRolloverFires) {
    // Target is 499 (wrapped), but now is only 498 — not there yet.
    uint32_t target = MAX - 500 + 1000;  // == 499
    EXPECT_FALSE(millisHasPassed(498, target));
}

TEST(MillisHasPassed, ReturnsFalseWhenNowIsHighAndTargetIsHigherStill) {
    // millis near MAX but hasn't reached target yet
    EXPECT_FALSE(millisHasPassed(MAX - 500, MAX - 100));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
