#include <gtest/gtest.h>
#include <helpers/MultiSerialInterface.h>

class FakeSerial : public BaseSerialInterface {
public:
  bool enabled = true;
  bool connected = false;
  void enable() override { enabled = true; }
  void disable() override { enabled = false; }
  bool isEnabled() const override { return enabled; }
  bool isConnected() const override { return connected; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t*, size_t size) override { return size; }
  size_t checkRecvFrame(uint8_t*) override { return 0; }
};

TEST(MultiSerialInterface, IgnoresConnectionReportedByDisabledBackend) {
  FakeSerial bluetooth;
  MultiSerialInterface serial;
  ASSERT_TRUE(serial.addInterface(InterfaceType::Bluetooth, &bluetooth));
  serial.enable();
  bluetooth.connected = true;
  ASSERT_TRUE(serial.isConnected());
  serial.disableBluetooth();
  ASSERT_TRUE(bluetooth.isConnected());
  EXPECT_FALSE(serial.isConnected());
}

TEST(MultiSerialInterface, KeepsAnEnabledConnectionWhenAnotherBackendIsDisabled) {
  FakeSerial bluetooth, usb;
  MultiSerialInterface serial;
  ASSERT_TRUE(serial.addInterface(InterfaceType::Bluetooth, &bluetooth));
  ASSERT_TRUE(serial.addInterface(InterfaceType::USB, &usb));
  serial.enable();
  bluetooth.connected = usb.connected = true;
  serial.disableBluetooth();
  EXPECT_TRUE(serial.isConnected());
  usb.connected = false;
  EXPECT_FALSE(serial.isConnected());
}

TEST(MultiSerialInterface, ReenabledBackendCanReportConnectionAgain) {
  FakeSerial bluetooth;
  MultiSerialInterface serial;
  ASSERT_TRUE(serial.addInterface(InterfaceType::Bluetooth, &bluetooth));
  serial.enable();
  bluetooth.connected = true;
  serial.disableBluetooth();
  EXPECT_FALSE(serial.isConnected());
  serial.enableBluetooth();
  EXPECT_TRUE(serial.isConnected());
}

TEST(MultiSerialInterface, DisabledManagerAndRemovedBackendsAreDisconnected) {
  FakeSerial usb;
  MultiSerialInterface serial;
  ASSERT_TRUE(serial.addInterface(InterfaceType::USB, &usb));
  serial.enable();
  usb.connected = true;
  serial.disable();
  EXPECT_FALSE(serial.isConnected());
  serial.enable();
  ASSERT_TRUE(serial.removeInterface(&usb));
  EXPECT_FALSE(serial.isConnected());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
