#include <gtest/gtest.h>

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

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    (void)freq;
    (void)bw;
    (void)sf;
    (void)cr;
  }

  float getCurrentRSSI() override { return -120.0f; }
  bool isReceivingPacket() override { return false; }

  void cachePacketMetrics(float rssi, float snr) {
    updateLastPacketMetrics(rssi, snr);
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
