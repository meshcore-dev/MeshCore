#include <Arduino.h>
#include <MeshCore.h>
#include <cmath>
#include <gtest/gtest.h>
#include <helpers/TimeSeriesData.h>
#include <utility>

static MinMaxAvg query(const TimeSeriesData &series, uint32_t now, uint32_t start, uint32_t end) {
  MinMaxAvg result;
  series.calcMinMaxAvg(now, start, end, &result, 200, 116);
  return result;
}

TEST(TimeSeriesData, RingWrapKeepsNewestSamples) {
  TimeSeriesData series(3, 10);
  for (uint32_t i = 1; i <= 5; i++)
    series.recordData(i * 10, float(i));
  auto result = query(series, 50, 100, 0);
  EXPECT_FLOAT_EQ(result._min, 3);
  EXPECT_FLOAT_EQ(result._max, 5);
  EXPECT_FLOAT_EQ(result._avg, 4);
  EXPECT_EQ(result._channel, 200);
  EXPECT_EQ(result._lpp_type, 116);
}

TEST(TimeSeriesData, RangeIncludesEndAndExcludesStart) {
  TimeSeriesData series(4, 10);
  series.recordData(10, -5);
  series.recordData(20, -3);
  series.recordData(30, -1);
  series.recordData(40, 7);
  auto result = query(series, 40, 30, 10);
  EXPECT_FLOAT_EQ(result._min, -3);
  EXPECT_FLOAT_EQ(result._max, -1);
  EXPECT_FLOAT_EQ(result._avg, -2);
}

TEST(TimeSeriesData, EmptyAndUnwrittenRangesReturnNaN) {
  TimeSeriesData series(4, 10);
  EXPECT_TRUE(std::isnan(query(series, 0, 100, 0)._avg));
  series.recordData(10, 4);
  for (auto range : { std::pair<uint32_t, uint32_t>(100, 1), { 0, 0 }, { 1, 10 } }) {
    auto result = query(series, 10, range.first, range.second);
    EXPECT_TRUE(std::isnan(result._min));
    EXPECT_TRUE(std::isnan(result._max));
    EXPECT_TRUE(std::isnan(result._avg));
  }
}

TEST(TimeSeriesData, CadenceAndNaNSamples) {
  float storage[3];
  TimeSeriesData series(storage, 3, 10);
  series.recordData(uint32_t(0), 2); // uptime zero is a valid first sample
  series.recordData(9, 100);
  EXPECT_FLOAT_EQ(query(series, 9, 100, 0)._avg, 2);
  series.recordData(10, 4);
  series.recordData(19, 100);
  series.recordData(20, NAN);
  series.recordData(20, 6);
  EXPECT_FLOAT_EQ(query(series, 20, 100, 0)._avg, 4);
}

TEST(TimeSeriesData, LargeGapsExpireOldSamplesAndDoNotStall) {
  TimeSeriesData series(4, 300);
  series.recordData(300, 1);
  const uint32_t later = UINT32_MAX / 300 * 300;
  series.recordData(later, 3);
  EXPECT_FLOAT_EQ(query(series, later, 301, 0)._avg, 3);
  EXPECT_FLOAT_EQ(query(series, later, UINT32_MAX, 0)._avg, 3);
  series.recordData(later + uint32_t(300), 5); // seconds counter wrap
  auto result = query(series, later + uint32_t(300), 301, 0);
  EXPECT_FLOAT_EQ(result._min, 3);
  EXPECT_FLOAT_EQ(result._max, 5);
  EXPECT_FLOAT_EQ(result._avg, 4);
}

TEST(TimeSeriesData, MissingIntervalsAreEmptyInsteadOfMovingOldSamplesForward) {
  TimeSeriesData series(5, 10);
  series.recordData(10, 2);
  series.recordData(40, 8);
  EXPECT_TRUE(std::isnan(query(series, 40, 30, 1)._avg));
  EXPECT_FLOAT_EQ(query(series, 40, 31, 0)._avg, 5);
  series.recordData(52, 10); // late read belongs to the fixed slot at 50
  EXPECT_FLOAT_EQ(query(series, 52, 3, 0)._avg, 10);
  EXPECT_TRUE(std::isnan(query(series, 52, 2, 0)._avg));
}

TEST(TimeSeriesData, QueryAgeDoesNotOverflowWhileWalkingTheRing) {
  TimeSeriesData series(3, 10);
  series.recordData(10, 1);
  series.recordData(20, 2);
  EXPECT_TRUE(std::isnan(query(series, uint32_t(20 + (UINT32_MAX - 5)), 100, 0)._avg));
}

TEST(TimeSeriesData, LegacyClockAPIUsesMonotonicTimeAcrossRTCAndMillisWrap) {
  class Clock : public mesh::RTCClock {
  public:
    uint32_t time = 100000;
    int reads = 0;
    uint32_t getCurrentTime() override {
      reads++;
      return time;
    }
    void setCurrentTime(uint32_t value) override { time = value; }
  } clock;
  TimeSeriesData series(3, 1);
  g_mock_millis = UINT32_MAX - 499;
  series.recordData(&clock, 2);
  clock.setCurrentTime(0);
  g_mock_millis = 500;
  series.recordData(&clock, 4);
  MinMaxAvg result;
  series.calcMinMaxAvg(&clock, 2, 0, &result, 1, 116);
  EXPECT_FLOAT_EQ(result._avg, 3);
  EXPECT_EQ(clock.reads, 0);
}

TEST(TimeSeriesData, MillisecondCounterWrapDoesNotResetSeconds) {
  TimeSeriesData series(3, 1);
  uint32_t last_millis = UINT32_MAX - 499;
  uint64_t uptime_millis = last_millis;
  series.recordData(uint32_t(uptime_millis / 1000), 1);
  uint32_t now = 500;
  uptime_millis += uint32_t(now - last_millis);
  series.recordData(uint32_t(uptime_millis / 1000), 3);
  EXPECT_FLOAT_EQ(query(series, uint32_t(uptime_millis / 1000), 2, 0)._avg, 2);
}

TEST(TimeSeriesData, ZeroCapacityIsEmpty) {
  TimeSeriesData series(0, 10);
  series.recordData(100, 1);
  EXPECT_TRUE(std::isnan(query(series, 100, 1000, 0)._avg));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
