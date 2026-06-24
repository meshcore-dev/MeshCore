#include <gtest/gtest.h>

#include <vector>

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

  void setCurrentRssiSamples(const std::vector<float>& samples) {
    current_rssi_samples = samples;
    current_rssi_index = 0;
  }

  void enterReceiveMode() {
    startRecv();
  }

  void collectNoiseFloorSamples(uint16_t sample_count = 64) {
    for (uint16_t i = 0; i < sample_count; i++) {
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
  samples.insert(samples.end(), 48, -103.0f);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(samples);
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-103, wrapper.getNoiseFloor());
}

TEST(RssiNoiseFloor, ClampedLowFloorDoesNotRejectLaterHealthySamples) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -130.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-120, wrapper.getNoiseFloor());

  wrapper.triggerNoiseFloorCalibrate(0);
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -102.0f));
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-102, wrapper.getNoiseFloor());
}

TEST(RssiNoiseFloor, VeryLowSamplesClampToLowerBound) {
  FakePhysicalLayer radio;
  FakeBoard board;
  TestRadioLibWrapper wrapper(radio, board);

  wrapper.begin();
  wrapper.setCurrentRssiSamples(std::vector<float>(64, -130.0f));
  wrapper.enterReceiveMode();
  wrapper.collectNoiseFloorSamples();

  EXPECT_EQ(-120, wrapper.getNoiseFloor());
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
