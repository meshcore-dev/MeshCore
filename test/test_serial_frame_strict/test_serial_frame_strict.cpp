// ArduinoSerialInterface built with SERIAL_FRAME_STRICT: a stalled (truncated) frame must not swallow the next frame,
// and a frame longer than MAX_FRAME_SIZE must be dropped, not executed truncated.
#define SERIAL_FRAME_STRICT
#include <gtest/gtest.h>
#include <vector>
#include "../../src/helpers/ArduinoSerialInterface.cpp"

namespace {
class FakeSerial : public Stream {
public:
  std::vector<uint8_t> in;
  size_t pos = 0;
  int available() override { return (int)(in.size() - pos); }
  int read() override { return pos < in.size() ? in[pos++] : -1; }
  void push(const std::vector<uint8_t>& b) { in.insert(in.end(), b.begin(), b.end()); }
};

std::vector<uint8_t> frame(const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> f = {'<', (uint8_t)(payload.size() & 0xFF), (uint8_t)(payload.size() >> 8)};
  f.insert(f.end(), payload.begin(), payload.end());
  return f;
}
}

TEST(SerialFrameStrict, CompleteFrameIsDelivered) {
  FakeSerial s;
  ArduinoSerialInterface sif;
  sif.begin(s);
  s.push(frame({22, 3}));
  uint8_t out[MAX_FRAME_SIZE];
  EXPECT_EQ(sif.checkRecvFrame(out), 2u);
  EXPECT_EQ(out[0], 22);
}

TEST(SerialFrameStrict, StalledFrameDoesNotSwallowTheNextOne) {
  FakeSerial s;
  ArduinoSerialInterface sif;
  sif.begin(s);
  uint8_t out[MAX_FRAME_SIZE];
  g_mock_millis = 1000;
  s.push({'<', 10, 0, 1, 2, 3});                 // header says 10 bytes, only 3 arrive
  EXPECT_EQ(sif.checkRecvFrame(out), 0u);
  g_mock_millis = 1200;                           // > 100 ms of silence: the partial frame is abandoned
  s.push(frame({22, 3}));
  EXPECT_EQ(sif.checkRecvFrame(out), 2u);
  EXPECT_EQ(out[0], 22);
}

TEST(SerialFrameStrict, OversizeFrameIsDroppedNotExecuted) {
  FakeSerial s;
  ArduinoSerialInterface sif;
  sif.begin(s);
  uint8_t out[MAX_FRAME_SIZE];
  std::vector<uint8_t> big(MAX_FRAME_SIZE + 20, 0x41);
  big[0] = 22;
  s.push(frame(big));
  EXPECT_EQ(sif.checkRecvFrame(out), 0u);         // dropped
  s.push(frame({5}));
  EXPECT_EQ(sif.checkRecvFrame(out), 1u);         // and the next frame still parses
  EXPECT_EQ(out[0], 5);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
