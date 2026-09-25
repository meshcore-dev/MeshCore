#include <gtest/gtest.h>
#include <helpers/radiolib/RadioLibWrappers.h>

class TestBoard : public mesh::MainBoard {
public:
  uint8_t startup = BD_STARTUP_NORMAL;
  int before = 0, after = 0;
  uint16_t getBattMilliVolts() override { return 4200; }
  const char* getManufacturerName() const override { return "test"; }
  void reboot() override { }
  uint8_t getStartupReason() const override { return startup; }
  void onBeforeTransmit() override { before++; }
  void onAfterTransmit() override { after++; }
};

class TestRadio : public RadioLibWrapper {
  bool _poll;
protected:
  bool isIRQPending() const override {
    return _poll ? _radio->pending : RadioLibWrapper::isIRQPending();
  }
  bool isRecvIRQPending() const override {
    return _poll ? isInRecvMode() && isIRQPending() : RadioLibWrapper::isRecvIRQPending();
  }
  bool isReceivingPacket() override { return false; }
public:
  TestRadio(PhysicalLayer& radio, TestBoard& board, bool poll)
    : RadioLibWrapper(radio, board), _poll(poll) { }
  void setParams(float, float, uint8_t, uint8_t) override { }
  float getCurrentRSSI() override { return -100; }
};

class RadioCompletion : public ::testing::TestWithParam<bool> { };

TEST_P(RadioCompletion, ReceiveAndTransmitEachCompleteOnceThenReturnToRX) {
  PhysicalLayer phy;
  TestBoard board;
  TestRadio radio(phy, board, GetParam());
  radio.begin();
  uint8_t bytes[8];
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_TRUE(radio.isInRecvMode());
  phy.pending = true;
  if (!GetParam()) phy.callback();
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 3);
  EXPECT_EQ(bytes[0], 0x12);
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_EQ(radio.getPacketsRecv(), 1U);
  ASSERT_TRUE(radio.startSendRaw(bytes, 3));
  EXPECT_FALSE(radio.isSendComplete());
  phy.pending = true;
  if (!GetParam()) phy.callback();
  EXPECT_TRUE(radio.isSendComplete());
  EXPECT_FALSE(radio.isSendComplete());
  EXPECT_EQ(radio.getPacketsSent(), 1U);
  radio.onSendFinished();
  EXPECT_EQ(board.before, 1);
  EXPECT_EQ(board.after, 1);
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_TRUE(radio.isInRecvMode());
}

TEST_P(RadioCompletion, PendingRXSurvivesAGCAndCRCFailureRestartsReception) {
  PhysicalLayer phy;
  TestBoard board;
  TestRadio radio(phy, board, GetParam());
  radio.begin();
  radio.resetStats();
  uint8_t bytes[8];
  radio.recvRaw(bytes, sizeof(bytes));
  phy.pending = true;
  if (!GetParam()) phy.callback();
  radio.resetAGC();
  EXPECT_EQ(phy.sleeps, 0);
  phy.read_error = -7;
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_EQ(radio.getPacketsRecvErrors(), 1U);
  EXPECT_TRUE(radio.isInRecvMode());
}

TEST_P(RadioCompletion, CADCompletionDoesNotBecomeAPacket) {
  PhysicalLayer phy;
  TestBoard board;
  TestRadio radio(phy, board, GetParam());
  radio.begin();
  radio.setCADEnabled(true);
  EXPECT_FALSE(radio.isChannelActive());
  uint8_t bytes[8];
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_EQ(phy.reads, 0);
}

TEST_P(RadioCompletion, FailedSendRejectsTXCompletionAndPreservesLatchedRX) {
  PhysicalLayer phy;
  TestBoard board;
  TestRadio radio(phy, board, GetParam());
  radio.begin();
  uint8_t bytes[8]{};
  ASSERT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  ASSERT_TRUE(radio.isInRecvMode());
  phy.tx_error = -1;
  ASSERT_FALSE(radio.startSendRaw(bytes, 3));
  ASSERT_FALSE(radio.isInRecvMode());
  // A delayed completion arrives after the failed send has put the wrapper in IDLE.
  phy.pending = true;
  if (!GetParam()) phy.callback();
  EXPECT_FALSE(radio.isSendComplete());
  EXPECT_EQ(radio.getPacketsSent(), 0U);
  EXPECT_EQ(board.after, 1);
  // Preserve latched interrupts on existing boards; W10 polling requires active RX.
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), GetParam() ? 0 : 3);
  EXPECT_EQ(phy.reads, GetParam() ? 0 : 1);
  EXPECT_EQ(radio.getPacketsRecv(), GetParam() ? 0U : 1U);
  if (!GetParam()) EXPECT_EQ(bytes[0], 0x12);
  EXPECT_TRUE(radio.isInRecvMode());
  EXPECT_EQ(phy.rx_starts, 2);
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_EQ(phy.reads, GetParam() ? 0 : 1);
}

INSTANTIATE_TEST_SUITE_P(InterruptAndPolling, RadioCompletion, ::testing::Bool());

TEST(RadioCompletion, DeepSleepWakeReportsRXAndDeliversTheAlreadyReceivedPacket) {
  PhysicalLayer phy;
  TestBoard board;
  board.startup = BD_STARTUP_RX_PACKET;
  TestRadio radio(phy, board, false);
  radio.begin();
  EXPECT_TRUE(radio.isInRecvMode());
  uint8_t bytes[8];
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 3);
  EXPECT_EQ(phy.reads, 1);
  EXPECT_TRUE(radio.isInRecvMode());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
