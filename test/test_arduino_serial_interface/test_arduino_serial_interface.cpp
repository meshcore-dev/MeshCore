#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "helpers/ArduinoSerialInterface.h"

namespace {

// A stream that accepts only `capacity` more bytes, the way a CDC port with a
// stalled host or a nearly full TX ring answers a write with a short count.
class CappedStream : public Stream {
public:
  explicit CappedStream(size_t capacity) : _capacity(capacity) {}

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buffer, size_t size) override {
    size_t n = size < _capacity ? size : _capacity;
    wire.insert(wire.end(), buffer, buffer + n);
    _capacity -= n;
    return n;
  }

  std::vector<uint8_t> wire;

private:
  size_t _capacity;
};

const uint8_t PAYLOAD[] = { 0x10, 0x20, 0x30, 0x40 };
const size_t LEN = sizeof(PAYLOAD);
const std::vector<uint8_t> FRAME_ON_WIRE = { '>', LEN, 0, 0x10, 0x20, 0x30, 0x40 };

size_t writeTo(CappedStream& stream) {
  ArduinoSerialInterface iface;
  iface.begin(stream);
  iface.enable();
  return iface.writeFrame(PAYLOAD, LEN);
}

}  // namespace

TEST(ArduinoSerialInterface, WholeFrameIsTakenAndReportedAsLen) {
  CappedStream stream(64);
  EXPECT_EQ(writeTo(stream), LEN);
  EXPECT_EQ(stream.wire, FRAME_ON_WIRE);
}

TEST(ArduinoSerialInterface, NothingWrittenReportsZeroSoTheCallerMayRetry) {
  CappedStream stream(0);
  EXPECT_EQ(writeTo(stream), 0u);
  EXPECT_TRUE(stream.wire.empty());
}

TEST(ArduinoSerialInterface, TornHeaderCountsAsTaken) {
  CappedStream stream(2);   // header cut short, payload never attempted
  EXPECT_EQ(writeTo(stream), LEN);
  EXPECT_EQ(stream.wire.size(), 2u);
}

TEST(ArduinoSerialInterface, TornPayloadCountsAsTaken) {
  CappedStream stream(3 + LEN - 1);   // header out, payload one byte short
  EXPECT_EQ(writeTo(stream), LEN);
  EXPECT_EQ(stream.wire.size(), 3 + LEN - 1);
}

TEST(ArduinoSerialInterface, OversizedFrameIsRefusedWithoutWriting) {
  CappedStream stream(1024);
  uint8_t big[MAX_FRAME_SIZE + 1] = {0};
  ArduinoSerialInterface iface;
  iface.begin(stream);
  iface.enable();
  EXPECT_EQ(iface.writeFrame(big, sizeof(big)), 0u);
  EXPECT_TRUE(stream.wire.empty());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
