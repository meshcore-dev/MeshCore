#include <gtest/gtest.h>
#include "helpers/esp32/SerialBLEInterface.h"

class TestBLEInterface : public SerialBLEInterface {
public:
  using SerialBLEInterface::onAuthenticationComplete;
  using SerialBLEInterface::onDisconnect;
};

class BLESendTest : public ::testing::Test {
protected:
  TestBLEInterface interface;
  uint8_t received[MAX_FRAME_SIZE] = {};
  BLECharacteristic& tx() { return BLEDevice::server.service.tx; }
  void SetUp() override {
    test_millis = 100;
    char name[] = "test";
    interface.begin("MeshCore-", name, 123456);
    interface.enable();
    interface.onAuthenticationComplete({true, 0});
    interface.checkRecvFrame(received);
  }
  void enqueue(uint8_t value) { ASSERT_EQ(interface.writeFrame(&value, 1), 1U); }
  size_t poll() { return interface.checkRecvFrame(received); }
  void advance() { test_millis += 60; }
};

TEST_F(BLESendTest, RetriesFailedNotificationInOrderWithoutDuplicateSuccess) {
  enqueue(0x18);
  enqueue(0x04);
  tx().result = BLECharacteristicCallbacks::ERROR_GATT;
  poll();
  EXPECT_TRUE(tx().sent.empty());
  EXPECT_EQ(tx().attempts, 1U);
  poll();
  EXPECT_EQ(tx().attempts, 1U); // preserve pacing even when the stack is congested
  advance();
  tx().result = BLECharacteristicCallbacks::SUCCESS_NOTIFY;
  poll();
  ASSERT_EQ(tx().sent.size(), 1U);
  EXPECT_EQ(tx().sent[0], std::vector<uint8_t>({0x18}));
  advance(); poll();
  ASSERT_EQ(tx().sent.size(), 2U);
  EXPECT_EQ(tx().sent[1], std::vector<uint8_t>({0x04}));
  advance(); poll();
  EXPECT_EQ(tx().sent.size(), 2U);
}

TEST_F(BLESendTest, WaitsForNotificationSubscription) {
  enqueue(0x13);
  tx().result = BLECharacteristicCallbacks::ERROR_NOTIFY_DISABLED;
  poll();
  advance();
  tx().result = BLECharacteristicCallbacks::SUCCESS_NOTIFY;
  poll();
  ASSERT_EQ(tx().sent.size(), 1U);
  EXPECT_EQ(tx().sent[0], std::vector<uint8_t>({0x13}));
}

TEST_F(BLESendTest, DoesNotReuseSuccessFromPreviousNotification) {
  enqueue(0x18); poll();
  enqueue(0x04);
  tx().report_status = false;
  advance(); poll();
  tx().report_status = true;
  advance(); poll();
  ASSERT_EQ(tx().sent.size(), 2U);
  EXPECT_EQ(tx().sent[1], std::vector<uint8_t>({0x04}));
}

TEST_F(BLESendTest, FullSendQueueDefersCommandsUntilAReplyFits) {
  for (uint8_t i = 0; i < 4; i++) enqueue(i);
  EXPECT_TRUE(interface.isWriteBusy());
  auto& rx = BLEDevice::server.service.rx;
  const uint8_t command[] = {31, 0};
  rx.setValue(command, sizeof(command));
  rx.callbacks->onWrite(&rx, nullptr);
  tx().result = BLECharacteristicCallbacks::ERROR_GATT;
  EXPECT_EQ(poll(), 0U);
  advance();
  EXPECT_TRUE(interface.isWriteBusy());
  tx().result = BLECharacteristicCallbacks::SUCCESS_NOTIFY;
  EXPECT_EQ(poll(), sizeof(command));
  EXPECT_EQ(received[0], 31);
  EXPECT_EQ(received[1], 0);
  enqueue(0x18); // space for the command's response was preserved
  for (int i = 0; i < 4; i++) { advance(); poll(); }
  ASSERT_EQ(tx().sent.size(), 5U);
  EXPECT_EQ(tx().sent[4], std::vector<uint8_t>({0x18}));
}

TEST_F(BLESendTest, DisconnectDiscardsFailedFramesBeforeNextSession) {
  enqueue(0x18);
  tx().result = BLECharacteristicCallbacks::ERROR_GATT;
  poll();
  interface.onDisconnect(&BLEDevice::server);
  tx().result = BLECharacteristicCallbacks::ERROR_NO_CLIENT;
  advance(); poll();
  interface.onAuthenticationComplete({true, 0});
  poll();
  tx().result = BLECharacteristicCallbacks::SUCCESS_NOTIFY;
  enqueue(0x05);
  advance(); poll();
  ASSERT_EQ(tx().sent.size(), 1U);
  EXPECT_EQ(tx().sent[0], std::vector<uint8_t>({0x05}));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
