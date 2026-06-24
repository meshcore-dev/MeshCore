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
  TestRadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  std::vector<float> current_rssi_samples;
  size_t current_rssi_index = 0;
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

  void cachePacketMetrics(float rssi, float snr) {
    updateLastPacketMetrics(rssi, snr);
  }

  void forceNoiseFloor(int16_t noise_floor) {
    _noise_floor = noise_floor;
    _num_floor_samples = NUM_NOISE_FLOOR_SAMPLES;
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

TEST(RssiNoiseFloor, LowStartupOutliersDoNotDominateMedianFloor) {
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
  EXPECT_EQ(-103, stats.sample_min);
  EXPECT_EQ(-103, stats.sample_median);
  EXPECT_EQ(-103, stats.sample_max);
  EXPECT_EQ(16, stats.rejected_low_bound_count);
}

TEST(RssiNoiseFloor, ClampedLowFloorDoesNotRejectLaterHealthySamples) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-120);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -102.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-102, wrapper.getNoiseFloor());
}

TEST(RssiNoiseFloor, ClampedLowFloorStillRejectsLaterLowBoundSamples) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-120);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -120.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(0, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(64, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, ClampedLowFloorStillRejectsStrongSamples) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloor(-120);
  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -58.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(0, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.rejected_low_bound_count);
  EXPECT_EQ(64, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, VeryLowSamplesDoNotPublishWhenNoPreviousFloorExists) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -130.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(0, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.sample_min);
  EXPECT_EQ(0, stats.sample_median);
  EXPECT_EQ(0, stats.sample_max);
  EXPECT_EQ(64, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, LowBoundRejectCountSaturates) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(70000, -130.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(70000);

  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(UINT16_MAX, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, RejectedOnlyBatchResetsOnCalibrationTrigger) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
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

TEST(RssiNoiseFloor, LowBoundStartupSamplesDoNotContaminateLaterHealthyBatch) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);
  std::vector<float> samples;

  samples.insert(samples.end(), 64, -120.0f);
  samples.insert(samples.end(), 64, -102.0f);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(samples);
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples(128);

  EXPECT_EQ(-102, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(64, stats.accepted_count);
  EXPECT_EQ(-102, stats.sample_min);
  EXPECT_EQ(-102, stats.sample_median);
  EXPECT_EQ(-102, stats.sample_max);
  EXPECT_EQ(64, stats.rejected_low_bound_count);
  EXPECT_EQ(0, stats.rejected_high_bound_count);
}

TEST(RssiNoiseFloor, CompletedLowBoundBatchIsNotReportedAsAccepted) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.forceNoiseFloorStats(-120, 64, -121, -120, -119, 0, 0);

  EXPECT_EQ(0, wrapper.getNoiseFloor());
  mesh::NoiseFloorStats stats = wrapper.getNoiseFloorStats();
  EXPECT_EQ(0, stats.accepted_count);
  EXPECT_EQ(0, stats.sample_min);
  EXPECT_EQ(0, stats.sample_median);
  EXPECT_EQ(0, stats.sample_max);
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
