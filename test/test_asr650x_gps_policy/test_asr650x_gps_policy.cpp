#include "helpers/asr650x/GpsPolicy.h"

#include <cstdio>
#include <gtest/gtest.h>

TEST(Asr650xGpsPolicy, Behaves) {
  /* default: enabled, interval 0, timeout 300 s */
  {
    asr650x::GpsPolicy p;
    p.configure(true, 0, 300);
    EXPECT_TRUE(p.state() == asr650x::GPS_OFF && !p.powerWanted()); // nothing happens before begin()
    p.begin(100);
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING && p.powerWanted());
    p.tick(110, false);
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING && p.warmingFor(110) == 10);
    p.tick(130, true); // fix -> done, power off
    EXPECT_TRUE(p.state() == asr650x::GPS_DONE && !p.powerWanted() && p.fixCount() == 1 && !p.timedOut());
    p.tick(100000, false); // interval 0: stays off forever
    EXPECT_TRUE(p.state() == asr650x::GPS_DONE && !p.powerWanted());
  }
  /* timeout without a fix */
  {
    asr650x::GpsPolicy p;
    p.configure(true, 0, 300);
    p.begin(0);
    p.tick(299, false);
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING);
    p.tick(300, false);
    EXPECT_TRUE(p.state() == asr650x::GPS_DONE && p.timedOut() && p.fixCount() == 0 && !p.powerWanted());
  }
  /* periodic refresh */
  {
    asr650x::GpsPolicy p;
    p.configure(true, 600, 300);
    p.begin(0);
    p.tick(20, true);
    EXPECT_TRUE(p.state() == asr650x::GPS_DONE);
    p.tick(619, false);
    EXPECT_TRUE(p.state() == asr650x::GPS_DONE);
    p.tick(620, false); // 600 s after entering DONE at t=20
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING && p.powerWanted());
    p.tick(640, true);
    EXPECT_TRUE(p.state() == asr650x::GPS_DONE && p.fixCount() == 2);
  }
  /* disabled: never powers, even on tick/begin */
  {
    asr650x::GpsPolicy p;
    p.configure(false, 60, 300);
    p.begin(0);
    p.tick(1000, true);
    EXPECT_TRUE(p.state() == asr650x::GPS_OFF && !p.powerWanted() && p.fixCount() == 0);
  }
  /* the app turns GPS on later: starts warming immediately; turning it off stops at once */
  {
    asr650x::GpsPolicy p;
    p.configure(false, 0, 300);
    p.begin(0);
    p.configure(true, 0, 300);
    p.request(50);
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING && p.powerWanted() && p.warmingFor(60) == 10);
    p.configure(false, 0, 300);
    EXPECT_TRUE(p.state() == asr650x::GPS_OFF && !p.powerWanted());
  }
  /* request() while already DONE re-acquires and clears the timeout flag */
  {
    asr650x::GpsPolicy p;
    p.configure(true, 0, 10);
    p.begin(0);
    p.tick(10, false);
    EXPECT_TRUE(p.timedOut());
    p.request(20);
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING && !p.timedOut());
  }
  /* clock going backwards (RTC set by the app) must not wedge the machine */
  {
    asr650x::GpsPolicy p;
    p.configure(true, 100, 300);
    p.begin(1000);
    p.tick(500, false); // now < start: treated as 0 s elapsed, no underflow
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING && p.warmingFor(500) == 0);
  }
  /* changing the interval while DONE applies to the next cycle */
  {
    asr650x::GpsPolicy p;
    p.configure(true, 0, 300);
    p.begin(0);
    p.tick(5, true);
    p.configure(true, 60, 300);
    p.tick(64, false);
    EXPECT_TRUE(p.state() == asr650x::GPS_DONE);
    p.tick(65, false);
    EXPECT_TRUE(p.state() == asr650x::GPS_WARMING);
  }
}

// the seconds counter wraps after 2^32 s: timeouts and intervals still fire across the wrap
TEST(Asr650xGpsPolicy, TimeoutAndIntervalAcrossCounterWrap) {
  asr650x::GpsPolicy p;
  p.configure(true, 600, 300);
  p.begin(0xFFFFFF00u);
  p.tick(0xFFFFFF00u + 299u, false);
  EXPECT_EQ(p.state(), asr650x::GPS_WARMING);
  p.tick(0xFFFFFF00u + 300u, false); // wrapped: 0x2C
  EXPECT_EQ(p.state(), asr650x::GPS_DONE);
  EXPECT_TRUE(p.timedOut());
  uint32_t done = 0xFFFFFF00u + 300u;
  p.tick(done + 599u, false);
  EXPECT_EQ(p.state(), asr650x::GPS_DONE);
  p.tick(done + 600u, false);
  EXPECT_EQ(p.state(), asr650x::GPS_WARMING);
  EXPECT_EQ(p.warmingFor(done + 610u), 10u);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
