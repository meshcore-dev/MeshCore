#include <gtest/gtest.h>
#include <Arduino.h>

// Model a piezo wired directly to a GPIO, without a separate enable pin.
#define PIN_BUZZER 22
#define OUTPUT 1
#define LOW 0

namespace {
int buzzerLevel = LOW;
void pinMode(int, int) { }
void digitalWrite(int, int level) { buzzerLevel = level; }
}

#include "../../src/helpers/ui/buzzer.cpp"

class BuzzerQuietTest : public testing::Test {
protected:
  genericBuzzer buzzer;

  void SetUp() override {
    rtttl::playing = false;
    rtttl::starts = rtttl::stops = 0;
    buzzer.begin();
  }
};

TEST_F(BuzzerQuietTest, QuietStopsAnActiveMelodyWithoutAnEnablePin) {
  buzzer.play("test:d=4,o=5,b=120:c");
  buzzerLevel = 1;
  ASSERT_TRUE(buzzer.isPlaying());

  buzzer.quiet(true);

  EXPECT_TRUE(buzzer.isQuiet());
  EXPECT_FALSE(buzzer.isPlaying());
  EXPECT_EQ(1, rtttl::stops);
  EXPECT_EQ(LOW, buzzerLevel);
  buzzer.loop();
  EXPECT_FALSE(buzzer.isPlaying());
}

TEST_F(BuzzerQuietTest, MutedPlayDoesNotStartAnotherMelody) {
  buzzer.quiet(true);
  buzzer.play("test:d=4,o=5,b=120:c");
  EXPECT_FALSE(buzzer.isPlaying());
  EXPECT_EQ(0, rtttl::starts);
}

TEST_F(BuzzerQuietTest, UnmutingAllowsTheNextMelody) {
  buzzer.play("test:d=4,o=5,b=120:c");
  buzzer.quiet(true);
  buzzer.quiet(false);
  buzzer.play("test:d=4,o=5,b=120:d");
  EXPECT_TRUE(buzzer.isPlaying());
  EXPECT_EQ(2, rtttl::starts);
}

TEST_F(BuzzerQuietTest, QuietWhenIdleLeavesTheOutputLow) {
  buzzerLevel = 1;
  buzzer.quiet(true);
  EXPECT_EQ(LOW, buzzerLevel);
  EXPECT_EQ(0, rtttl::stops);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
