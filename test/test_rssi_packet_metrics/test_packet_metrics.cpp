#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "helpers/StatsFormatHelper.h"
#include "helpers/radiolib/RadioLibWrappers.h"

#include "../../src/helpers/radiolib/RadioLibWrappers.cpp"

class FakeBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 0; }
  const char* getManufacturerName() const override { return "test"; }
  void reboot() override { }
  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
};

class FakePhysicalLayer : public PhysicalLayer {
public:
  float packet_rssi = -73.0f;
  float packet_snr = 8.25f;

  float getRSSI() override { return packet_rssi; }
  float getSNR() override { return packet_snr; }
};

class TestRadioLibWrapper : public RadioLibWrapper {
public:
  TestRadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) {
    setNoiseFloorCalibration(0, 30000);
  }

  std::vector<float> current_rssi_samples;
  size_t current_rssi_index = 0;
  unsigned long current_millis = 0;
  bool receiving_packet = false;

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    (void)freq;
    (void)bw;
    (void)sf;
    (void)cr;
  }

  float getCurrentRSSI() override {
    if (current_rssi_index < current_rssi_samples.size()) {
      return current_rssi_samples[current_rssi_index++];
    }
    return -120.0f;
  }

  bool isReceivingPacket() override { return receiving_packet; }

  unsigned long getMillis() const override { return current_millis; }

  void advanceMillis(unsigned long millis) {
    current_millis += millis;
  }

  void cachePacketMetrics(float rssi, float snr) {
    updateLastPacketMetrics(rssi, snr);
  }

  void forceNoiseFloor(int16_t noise_floor) {
    _noise_floor = noise_floor;
    _num_floor_samples = NUM_NOISE_FLOOR_SAMPLES;
  }

  void forceNoiseFloorStats(int16_t noise_floor, uint16_t accepted_count,
                            int16_t sample_min, int16_t sample_median,
                            int16_t sample_max, uint16_t rejected_low_bound,
                            uint16_t rejected_high_bound) {
    _noise_floor = noise_floor;
    _num_floor_samples = accepted_count;
    _floor_sample_min = sample_min;
    _floor_sample_median = sample_median;
    _floor_sample_max = sample_max;
    _floor_rejected_low_bound = rejected_low_bound;
    _floor_rejected_high_bound = rejected_high_bound;
  }

  void setCurrentRssiSamples(const std::vector<float>& samples) {
    current_rssi_samples = samples;
    current_rssi_index = 0;
  }

  void enterReceiveMode() {
    startRecv();
  }

  void collectNoiseFloorSamples(uint32_t sample_count = 64) {
    for (uint32_t i = 0; i < sample_count; i++) {
      loop();
    }
  }
};

TEST(RssiPacketMetrics, CachedPacketMetricsAreReportedAsLastMetrics) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.cachePacketMetrics(radio.getRSSI(), radio.getSNR());

  EXPECT_EQ(-73.0f, wrapper.getLastRSSI());
  EXPECT_EQ(8.25f, wrapper.getLastSNR());
}

TEST(RssiPacketMetrics, LaterRadioStatusDoesNotChangeCachedPacketMetrics) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.cachePacketMetrics(radio.getRSSI(), radio.getSNR());

  radio.packet_rssi = -120.0f;
  radio.packet_snr = 0.0f;

  EXPECT_EQ(-73.0f, wrapper.getLastRSSI());
  EXPECT_EQ(8.25f, wrapper.getLastSNR());
}

TEST(RssiPacketMetrics, NewPacketMetricsReplacePreviousPacketMetrics) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.cachePacketMetrics(radio.getRSSI(), radio.getSNR());

  wrapper.cachePacketMetrics(-91.0f, -3.5f);

  EXPECT_EQ(-91.0f, wrapper.getLastRSSI());
  EXPECT_EQ(-3.5f, wrapper.getLastSNR());
}

TEST(RssiNoiseFloor, LowStartupSamplesDoNotDominateLowerQuartileFloor) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);
  std::vector<float> samples;

  samples.insert(samples.end(), 16, -130.0f);
  samples.insert(samples.end(), 64, -103.0f);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(samples);
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(80);

  EXPECT_EQ(-103, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-125, stats.sample_min);
  EXPECT_EQ(-103, stats.sample_median);
  EXPECT_EQ(-103, stats.sample_max);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
}

TEST(RssiNoiseFloor, LowFloorDoesNotRejectLaterHealthySamples) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-120);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -112.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-112, wrapper.getNoiseFloor());
}

TEST(RssiNoiseFloor, LowFloorAcceptsLaterAnalyserPlausibleLowSamples) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-120);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -120.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-120, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, LowFloorStillRejectsLargeUpwardJumps) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-120);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -58.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-120, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(64, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, VeryLowSamplesUseDefaultLowClampAndCanPublishWhenNoPreviousFloorExists) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -140.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-125, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-125, stats.sample_min);
  EXPECT_EQ(-125, stats.sample_median);
  EXPECT_EQ(-125, stats.sample_max);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, RuntimeLowClampOverridesDefaultClamp) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setNoiseFloorClamps(-135, -80);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -140.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-135, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-135, stats.sample_min);
  EXPECT_EQ(-135, stats.sample_median);
  EXPECT_EQ(-135, stats.sample_max);
}

TEST(RssiNoiseFloor, RuntimeHighClampRejectsConfiguredActivityBound) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setNoiseFloorClamps(-125, -90);
  wrapper.setCurrentRssiSamples(std::vector<float>(12, -89.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(12);

  EXPECT_EQ(0, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(12, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, LowBoundRejectCountSaturates) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-100);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(70000, -130.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(70000);

  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(UINT16_MAX, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, PartialBatchMedianReflectsAcceptedSamples) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples({-110.0f, -100.0f, -90.0f});
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(3);

  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(3, stats.accepted_count);
  EXPECT_EQ(-110, stats.sample_min);
  EXPECT_EQ(-100, stats.sample_median);
  EXPECT_EQ(-90, stats.sample_max);
}

TEST(RssiNoiseFloor, RejectedOnlyBatchResetsOnCalibrationTrigger) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-100);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(12, -130.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(12);

  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(12, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);

  wrapper.triggerNoiseFloorCalibrate(0);

  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, StrongStartupRssiDoesNotPublishWhenNoPreviousFloorExists) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -58.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(0, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.sample_min);
  EXPECT_EQ(0, stats.sample_median);
  EXPECT_EQ(0, stats.sample_max);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(64, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, StrongRssiSamplesDoNotContaminateHealthyBatch) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);
  std::vector<float> samples;

  samples.insert(samples.end(), 8, -58.0f);
  samples.insert(samples.end(), 64, -100.0f);

  wrapper.begin();
  wrapper.forceNoiseFloor(-100);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(samples);
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(72);

  EXPECT_EQ(-100, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-100, stats.sample_min);
  EXPECT_EQ(-100, stats.sample_median);
  EXPECT_EQ(-100, stats.sample_max);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(8, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, RejectedHighOnlyBatchResetsOnCalibrationTrigger) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(12, -58.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(12);

  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(12, stats.rejected_high_bound_count);

  wrapper.triggerNoiseFloorCalibrate(0);

  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, LowBoundStartupSamplesCanPublishAnalyserPlausibleFloor) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -120.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-120, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-120, stats.sample_min);
  EXPECT_EQ(-120, stats.sample_median);
  EXPECT_EQ(-120, stats.sample_max);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, LowerQuartileResistsIntermittentHighRssi) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);
  std::vector<float> samples;

  samples.insert(samples.end(), 20, -118.0f);
  samples.insert(samples.end(), 44, -104.0f);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(samples);
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-118, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-118, stats.sample_min);
  EXPECT_EQ(-104, stats.sample_median);
  EXPECT_EQ(-104, stats.sample_max);
}

TEST(RssiNoiseFloor, CompletedLowBoundBatchIsReportedAsAccepted) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloorStats(-120, 64, -121, -120, -119, 0, 0);

  EXPECT_EQ(-120, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-121, stats.sample_min);
  EXPECT_EQ(-120, stats.sample_median);
  EXPECT_EQ(-119, stats.sample_max);
}

TEST(RssiNoiseFloor, ResetAGCPreservesPreviousFloorUntilValidBatchCompletes) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-103);
  wrapper.resetAGC();
  wrapper.enterReceiveMode();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -130.0f));
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-103, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.sample_min);
  EXPECT_EQ(0, stats.sample_median);
  EXPECT_EQ(0, stats.sample_max);
  EXPECT_EQ(64, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, ReceivingPacketSkipsNoiseFloorSampling) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -101.0f));
  wrapper.enterReceiveMode();

  wrapper.receiving_packet = true;
  wrapper.collectNoiseFloorSamples(10);

  EXPECT_EQ(0, wrapper.current_rssi_index);
  EXPECT_EQ(0, wrapper.getNoiseFloor());

  wrapper.receiving_packet = false;
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-101, wrapper.getNoiseFloor());
}

TEST(RssiNoiseFloor, SamplingIsRateLimited) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setNoiseFloorCalibration(250, 30000);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -101.0f));
  wrapper.enterReceiveMode();

  wrapper.collectNoiseFloorSamples(10);
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(1, stats.accepted_count);

  wrapper.advanceMillis(249);
  wrapper.collectNoiseFloorSamples(10);
  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(1, stats.accepted_count);

  wrapper.advanceMillis(1);
  wrapper.collectNoiseFloorSamples(10);
  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(2, stats.accepted_count);
}

TEST(RssiNoiseFloor, ScheduledCalibrationRefreshResetsAfterSettleDelay) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setNoiseFloorCalibration(1000, 30000);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -101.0f));
  wrapper.enterReceiveMode();

  wrapper.collectNoiseFloorSamples(1);
  wrapper.advanceMillis(1000);
  wrapper.collectNoiseFloorSamples(1);
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(2, stats.accepted_count);

  wrapper.scheduleNoiseFloorCalibration(500);
  wrapper.receiving_packet = true;
  wrapper.advanceMillis(499);
  wrapper.collectNoiseFloorSamples(1);
  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(2, stats.accepted_count);

  wrapper.advanceMillis(1);
  wrapper.collectNoiseFloorSamples(1);
  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);

  wrapper.receiving_packet = false;
  wrapper.collectNoiseFloorSamples(1);
  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(1, stats.accepted_count);
}

TEST(RssiNoiseFloor, CalibrationWindowDropsStalePartialBatch) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setNoiseFloorCalibration(250, 1000);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -101.0f));
  wrapper.enterReceiveMode();

  wrapper.collectNoiseFloorSamples();
  wrapper.advanceMillis(250);
  wrapper.collectNoiseFloorSamples();

  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(2, stats.accepted_count);

  wrapper.advanceMillis(750);
  wrapper.collectNoiseFloorSamples(1);

  stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.sample_min);
  EXPECT_EQ(0, stats.sample_median);
  EXPECT_EQ(0, stats.sample_max);
}

TEST(RssiNoiseFloor, RadioStatsExposeCalibrationDiagnostics) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);
  char reply[512];

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -101.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  StatsFormatHelper::formatRadioStats(reply, &wrapper, wrapper, 1000, 2000);

  EXPECT_NE(nullptr, std::strstr(reply, "\"noise_floor_sample_count\":64"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"noise_floor_sample_min\":-101"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"noise_floor_sample_median\":-101"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"noise_floor_sample_max\":-101"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"noise_floor_rejected_low_bound\":0"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"noise_floor_rejected_high_bound\":0"));
}

TEST(RssiNoiseFloor, NoiseStatsExposeCalibrationDiagnostics) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);
  char reply[512];

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -101.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  StatsFormatHelper::formatNoiseFloorStats(reply, &wrapper);

  EXPECT_NE(nullptr, std::strstr(reply, "\"floor\":-101"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"accepted\":64"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"min\":-101"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"median\":-101"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"max\":-101"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"rejected_low\":0"));
  EXPECT_NE(nullptr, std::strstr(reply, "\"rejected_high\":0"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
