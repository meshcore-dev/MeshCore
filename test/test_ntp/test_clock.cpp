#include <gtest/gtest.h>
#include <helpers/ntp/NtpClock.h>

class TestClock : public mesh::RTCClock {
public:
  uint32_t now = 1000;
  int adjustments = 0;

  uint32_t getCurrentTime() override { return now; }
  void setCurrentTime(uint32_t time) override {
    now = time;
    ++adjustments;
  }
};

TEST(NtpClock, CorrectsAheadAndBehindClocks) {
  TestClock clock;
  ntp::LastReply reply;
  EXPECT_EQ(reply.received, 0U);

  reply.accept(clock, 900);
  EXPECT_EQ(reply.received, 900U);
  EXPECT_EQ(clock.now, 900U);
  EXPECT_EQ(clock.adjustments, 1);

  reply.accept(clock, 900);
  EXPECT_EQ(clock.adjustments, 1);

  reply.accept(clock, 1100);
  EXPECT_EQ(reply.received, 1100U);
  EXPECT_EQ(clock.now, 1100U);
  EXPECT_EQ(clock.adjustments, 2);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
