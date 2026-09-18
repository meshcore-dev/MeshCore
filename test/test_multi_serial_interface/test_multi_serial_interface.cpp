#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "helpers/MultiSerialInterface.h"

namespace {

// A transport that either takes every frame whole or refuses them all, the way
// a BLE/WiFi interface behaves with no client attached or a full send queue.
class FakeInterface : public BaseSerialInterface {
public:
  explicit FakeInterface(bool accepts) : _accepts(accepts) {}

  void enable() override { _enabled = true; }
  void disable() override { _enabled = false; }
  bool isEnabled() const override { return _enabled; }
  bool isConnected() const override { return _accepts; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t src[], size_t len) override {
    writes++;
    if (!_accepts) return 0;
    last.assign(src, src + len);
    return len;
  }
  size_t checkRecvFrame(uint8_t dest[]) override { return 0; }

  int writes = 0;
  std::vector<uint8_t> last;

private:
  bool _accepts;
  bool _enabled = false;
};

const uint8_t FRAME[] = { 0x10, 0x20, 0x30 };
const std::vector<uint8_t> FRAME_BYTES(FRAME, FRAME + sizeof(FRAME));

}  // namespace

TEST(MultiSerialInterface, DeliveredWhenOneOfTwoEnabledInterfacesAcceptsIt) {
  FakeInterface ble(false);   // advertising, nobody connected
  FakeInterface usb(true);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::Bluetooth, &ble);
  multi.addInterface(InterfaceType::USB, &usb);
  multi.enable();

  EXPECT_EQ(multi.writeFrame(FRAME, sizeof(FRAME)), sizeof(FRAME));
  EXPECT_EQ(ble.writes, 1);   // still offered to every enabled interface
  EXPECT_EQ(usb.writes, 1);
  EXPECT_EQ(usb.last, FRAME_BYTES);
}

TEST(MultiSerialInterface, NotDeliveredWhenNoInterfaceAcceptsIt) {
  FakeInterface ble(false);
  FakeInterface usb(false);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::Bluetooth, &ble);
  multi.addInterface(InterfaceType::USB, &usb);
  multi.enable();

  EXPECT_EQ(multi.writeFrame(FRAME, sizeof(FRAME)), 0u);
  EXPECT_EQ(ble.writes, 1);
  EXPECT_EQ(usb.writes, 1);
}

TEST(MultiSerialInterface, DisabledInterfaceIsNeitherWrittenNorCounted) {
  FakeInterface ble(true);
  FakeInterface usb(false);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::Bluetooth, &ble);
  multi.addInterface(InterfaceType::USB, &usb);
  multi.enable();
  ble.disable();

  EXPECT_EQ(multi.writeFrame(FRAME, sizeof(FRAME)), 0u);
  EXPECT_EQ(ble.writes, 0);
  EXPECT_EQ(usb.writes, 1);
}

TEST(MultiSerialInterface, NothingIsWrittenWhileDisabledOrEmpty) {
  FakeInterface usb(true);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::USB, &usb);

  EXPECT_EQ(multi.writeFrame(FRAME, sizeof(FRAME)), 0u);   // manager not enabled
  multi.enable();
  EXPECT_EQ(multi.writeFrame(FRAME, 0), 0u);               // empty frame
  EXPECT_EQ(usb.writes, 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
