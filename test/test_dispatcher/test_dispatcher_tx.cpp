#include <gtest/gtest.h>

#include "Dispatcher.h"
#include "helpers/StaticPoolPacketManager.h"

class TestClock : public mesh::MillisecondClock {
public:
  unsigned long now = 0;

  unsigned long getMillis() override { return now; }
};

class TestRadio : public mesh::Radio {
public:
  bool start_success = true;
  bool send_complete = false;
  int start_count = 0;
  int finish_count = 0;

  int recvRaw(uint8_t*, int) override { return 0; }
  uint32_t getEstAirtimeFor(int) override { return 10; }
  float packetScore(float, int) override { return 0; }
  bool startSendRaw(const uint8_t*, int) override {
    start_count++;
    return start_success;
  }
  bool isSendComplete() override { return send_complete; }
  void onSendFinished() override { finish_count++; }
  bool isInRecvMode() const override { return true; }
};

class TestDispatcher : public mesh::Dispatcher {
public:
  int status_count = 0;
  int free_count_at_status = -1;
  mesh::PacketTxStatus last_status = mesh::PACKET_TX_COMPLETE;

  TestDispatcher(TestRadio& radio, TestClock& clock, mesh::PacketManager& manager)
      : mesh::Dispatcher(radio, clock, manager), manager(manager) {}

protected:
  mesh::DispatcherAction onRecvPacket(mesh::Packet*) override { return ACTION_RELEASE; }
  void onPacketTxStatus(mesh::Packet*, mesh::PacketTxStatus status) override {
    status_count++;
    free_count_at_status = manager.getFreeCount();
    last_status = status;
  }

private:
  mesh::PacketManager& manager;
};

static mesh::Packet* queueTestPacket(TestDispatcher& dispatcher) {
  mesh::Packet* packet = dispatcher.obtainNewPacket();
  if (!packet) return NULL;

  packet->header = PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT;
  packet->path_len = 0;
  packet->payload[0] = 0x42;
  packet->payload_len = 1;
  dispatcher.sendPacket(packet, 0);
  return packet;
}

TEST(DispatcherTxStatus, ReportsCompletionBeforeReleasingPacket) {
  TestClock clock;
  TestRadio radio;
  StaticPoolPacketManager manager(1);
  TestDispatcher dispatcher(radio, clock, manager);
  dispatcher.begin();

  ASSERT_NE(nullptr, queueTestPacket(dispatcher));
  clock.now = 1;
  dispatcher.loop();
  EXPECT_EQ(1, radio.start_count);
  EXPECT_EQ(0, dispatcher.status_count);

  radio.send_complete = true;
  clock.now = 2;
  dispatcher.loop();

  EXPECT_EQ(1, dispatcher.status_count);
  EXPECT_EQ(mesh::PACKET_TX_COMPLETE, dispatcher.last_status);
  EXPECT_EQ(0, dispatcher.free_count_at_status);
  EXPECT_EQ(1, radio.finish_count);
  EXPECT_EQ(1, manager.getFreeCount());
}

TEST(DispatcherTxStatus, DistinguishesStartFailure) {
  TestClock clock;
  TestRadio radio;
  radio.start_success = false;
  StaticPoolPacketManager manager(1);
  TestDispatcher dispatcher(radio, clock, manager);
  dispatcher.begin();

  ASSERT_NE(nullptr, queueTestPacket(dispatcher));
  clock.now = 1;
  dispatcher.loop();

  EXPECT_EQ(1, dispatcher.status_count);
  EXPECT_EQ(mesh::PACKET_TX_START_FAILED, dispatcher.last_status);
  EXPECT_EQ(0, dispatcher.free_count_at_status);
  EXPECT_EQ(0, radio.finish_count);
  EXPECT_EQ(1, manager.getFreeCount());
}

TEST(DispatcherTxStatus, ReportsCompletionTimeoutAsUnknownOutcome) {
  TestClock clock;
  TestRadio radio;
  StaticPoolPacketManager manager(1);
  TestDispatcher dispatcher(radio, clock, manager);
  dispatcher.begin();

  ASSERT_NE(nullptr, queueTestPacket(dispatcher));
  clock.now = 1;
  dispatcher.loop();
  ASSERT_EQ(1, radio.start_count);

  clock.now = 17;  // estimated airtime is 10 ms; dispatcher timeout is 1.5x
  dispatcher.loop();

  EXPECT_EQ(1, dispatcher.status_count);
  EXPECT_EQ(mesh::PACKET_TX_TIMEOUT, dispatcher.last_status);
  EXPECT_EQ(0, dispatcher.free_count_at_status);
  EXPECT_EQ(1, radio.finish_count);
  EXPECT_EQ(1, manager.getFreeCount());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
