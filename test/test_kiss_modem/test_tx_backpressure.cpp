#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <vector>

#include "KissModem.h"

static constexpr int TEST_TX_AVAILABLE_BYTES = 4096;
static constexpr size_t TEST_DEFAULT_MAX_WRITE_CHUNK = SIZE_MAX;
static constexpr size_t TEST_PARTIAL_WRITE_CHUNK = 2;
static constexpr int TEST_PARTIAL_WRITE_FLUSH_LOOPS = 3;
static constexpr uint8_t TEST_SNR = 8;
static constexpr uint8_t TEST_RSSI = 200;

class BlockingStream : public Stream {
public:
  void pushRx(const std::vector<uint8_t>& bytes) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (uint8_t b : bytes) {
      _rx.push(b);
    }
  }

  void setBlockWrites(bool blocked) {
    {
      std::lock_guard<std::mutex> lock(_mutex);
      _block_writes = blocked;
    }
    _cv.notify_all();
  }

  bool isWriteBlocked() const {
    return _entered_block.load();
  }

  size_t writesCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _writes.size();
  }

  std::vector<uint8_t> writesSnapshot() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _writes;
  }

  int availableForWrite() override {
    std::lock_guard<std::mutex> lock(_mutex);
    return _block_writes ? 0 : TEST_TX_AVAILABLE_BYTES;
  }

  void setMaxWriteChunk(size_t chunk) {
    std::lock_guard<std::mutex> lock(_mutex);
    _max_write_chunk = chunk;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    std::unique_lock<std::mutex> lock(_mutex);
    while (_block_writes) {
      _entered_block.store(true);
      _cv.wait(lock);
    }
    const size_t chunk = (size < _max_write_chunk) ? size : _max_write_chunk;
    for (size_t i = 0; i < chunk; i++) {
      _writes.push_back(buffer[i]);
    }
    return chunk;
  }

  size_t write(uint8_t b) override {
    return write(&b, 1);
  }

  int available() override {
    std::lock_guard<std::mutex> lock(_mutex);
    return static_cast<int>(_rx.size());
  }

  int read() override {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_rx.empty()) {
      return -1;
    }
    int b = _rx.front();
    _rx.pop();
    return b;
  }

private:
  mutable std::mutex _mutex;
  std::condition_variable _cv;
  std::queue<uint8_t> _rx;
  std::vector<uint8_t> _writes;
  bool _block_writes = false;
  std::atomic<bool> _entered_block = false;
  size_t _max_write_chunk = TEST_DEFAULT_MAX_WRITE_CHUNK;
};

class FakeRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override {
    for (size_t i = 0; i < sz; i++) {
      dest[i] = 0;
    }
  }
};

class FakeRadio : public mesh::Radio {
public:
  bool isReceiving() override { return false; }
  uint32_t getEstAirtimeFor(uint16_t) override { return 10; }
  bool startSendRaw(const uint8_t*, uint16_t) override {
    _start_send_count++;
    return _start_send_result;
  }
  bool isSendComplete() override { return _send_complete; }
  void onSendFinished() override { _send_finished_count++; }
  int16_t getNoiseFloor() override { return -120; }

  void setStartSendResult(bool result) { _start_send_result = result; }
  void setSendComplete(bool complete) { _send_complete = complete; }
  int startSendCount() const { return _start_send_count; }
  int sendFinishedCount() const { return _send_finished_count; }

private:
  bool _start_send_result = true;
  bool _send_complete = true;
  int _start_send_count = 0;
  int _send_finished_count = 0;
};

class FakeBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 4200; }
  float getMCUTemperature() override { return 24.0f; }
  const char* getManufacturerName() override { return "test-board"; }
  void reboot() override {}
};

class FakeSensors : public SensorManager {
public:
  bool querySensors(uint8_t, CayenneLPP&) override { return false; }
};

class KissModemFixture : public ::testing::Test {
protected:
  BlockingStream serial;
  mesh::LocalIdentity identity;
  FakeRNG rng;
  FakeRadio radio;
  FakeBoard board;
  FakeSensors sensors;
  KissModem modem;

  KissModemFixture()
    : modem(serial, identity, rng, radio, board, sensors) {
    modem.begin();
  }

  static std::vector<uint8_t> dataFrame(const std::vector<uint8_t>& packet) {
    std::vector<uint8_t> frame = {KISS_FEND, KISS_CMD_DATA};
    frame.insert(frame.end(), packet.begin(), packet.end());
    frame.push_back(KISS_FEND);
    return frame;
  }

  void advanceToTxSending() {
    modem.loop();
    modem.loop();
    delay((uint32_t)KISS_DEFAULT_TXDELAY * 10);
    modem.loop();
  }
};

TEST_F(KissModemFixture, PingResponseShouldNotStallLoopUnderTxBackpressure) {
  serial.setBlockWrites(true);
  serial.pushRx({KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_PING, KISS_FEND});

  auto future = std::async(std::launch::async, [this]() {
    modem.loop();
  });

  auto status = future.wait_for(std::chrono::milliseconds(100));
  EXPECT_EQ(status, std::future_status::ready) << "KissModem::loop blocked in serial write under TX backpressure";
  EXPECT_FALSE(serial.isWriteBlocked()) << "KissModem entered blocking write path";

  serial.setBlockWrites(false);
  future.wait();
  modem.loop();
  EXPECT_GT(serial.writesCount(), 0U) << "KissModem did not flush queued response after backpressure cleared";
}

TEST_F(KissModemFixture, PingResponseKeepsStandardKissFraming) {
  serial.pushRx({KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_PING, KISS_FEND});
  modem.loop();

  const std::vector<uint8_t> expected = {KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP(HW_CMD_PING), KISS_FEND};
  EXPECT_EQ(serial.writesSnapshot(), expected);
}

TEST_F(KissModemFixture, PingResponseKeepsFramingWithPartialBulkWrites) {
  serial.setMaxWriteChunk(TEST_PARTIAL_WRITE_CHUNK);
  serial.pushRx({KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_PING, KISS_FEND});
  for (int i = 0; i < TEST_PARTIAL_WRITE_FLUSH_LOOPS; i++) {
    modem.loop();
  }

  const std::vector<uint8_t> expected = {KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP(HW_CMD_PING), KISS_FEND};
  EXPECT_EQ(serial.writesSnapshot(), expected);
}

TEST_F(KissModemFixture, PacketAndMetaAreQueuedTogetherUnderBackpressure) {
  static constexpr uint8_t TEST_PACKET[] = {0x01, 0x02, 0x03};

  serial.setBlockWrites(true);
  modem.onPacketReceived((int8_t)TEST_SNR, (int8_t)TEST_RSSI, TEST_PACKET, sizeof(TEST_PACKET));
  serial.setBlockWrites(false);
  modem.loop();
  modem.loop();

  const std::vector<uint8_t> expected = {
      KISS_FEND, KISS_CMD_DATA, TEST_PACKET[0], TEST_PACKET[1], TEST_PACKET[2], KISS_FEND,
      KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, TEST_SNR, TEST_RSSI, KISS_FEND};
  EXPECT_EQ(serial.writesSnapshot(), expected);
}

TEST_F(KissModemFixture, RadioTxCompletionAdvancesWhileHostOutputIsBackedUp) {
  serial.pushRx(dataFrame({0x42}));
  advanceToTxSending();
  ASSERT_EQ(radio.startSendCount(), 1);

  serial.setBlockWrites(true);
  modem.loop();
  EXPECT_EQ(radio.sendFinishedCount(), 1);
  EXPECT_TRUE(modem.isTxBusy());

  serial.setBlockWrites(false);
  modem.loop();

  const std::vector<uint8_t> expected = {KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_TX_DONE, 0x01, KISS_FEND};
  EXPECT_EQ(serial.writesSnapshot(), expected);
  EXPECT_FALSE(modem.isTxBusy());
}

TEST_F(KissModemFixture, QueueFullReportsBusyWithoutDroppingQueuedFrames) {
  static constexpr uint8_t TEST_PACKET_ONE[] = {0x11, 0x12};
  static constexpr uint8_t TEST_PACKET_TWO[] = {0x21, 0x22};

  serial.setBlockWrites(true);
  modem.onPacketReceived((int8_t)TEST_SNR, (int8_t)TEST_RSSI, TEST_PACKET_ONE, sizeof(TEST_PACKET_ONE));
  modem.onPacketReceived((int8_t)TEST_SNR, (int8_t)TEST_RSSI, TEST_PACKET_TWO, sizeof(TEST_PACKET_TWO));
  serial.setBlockWrites(false);
  modem.loop();

  const std::vector<uint8_t> expected = {
      KISS_FEND, KISS_CMD_DATA, TEST_PACKET_ONE[0], TEST_PACKET_ONE[1], KISS_FEND,
      KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, TEST_SNR, TEST_RSSI, KISS_FEND,
      KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_ERROR, HW_ERR_TX_BUSY, KISS_FEND};
  EXPECT_EQ(serial.writesSnapshot(), expected);
}

TEST_F(KissModemFixture, QueuedEncoderEscapesKissSpecialBytes) {
  static constexpr uint8_t TEST_PACKET[] = {KISS_FEND, KISS_FESC, 0x01};

  modem.onPacketReceived((int8_t)TEST_SNR, (int8_t)TEST_RSSI, TEST_PACKET, sizeof(TEST_PACKET));

  const std::vector<uint8_t> expected = {
      KISS_FEND, KISS_CMD_DATA, KISS_FESC, KISS_TFEND, KISS_FESC, KISS_TFESC, 0x01, KISS_FEND,
      KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, TEST_SNR, TEST_RSSI, KISS_FEND};
  EXPECT_EQ(serial.writesSnapshot(), expected);
}

TEST_F(KissModemFixture, MaxPacketWorstCaseEscapingFitsQueuedFrame) {
  std::vector<uint8_t> packet(KISS_MAX_PACKET_SIZE, KISS_FEND);
  std::vector<uint8_t> expected = {KISS_FEND, KISS_CMD_DATA};
  for (size_t i = 0; i < packet.size(); i++) {
    expected.push_back(KISS_FESC);
    expected.push_back(KISS_TFEND);
  }
  expected.push_back(KISS_FEND);
  expected.insert(expected.end(), {KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, TEST_SNR, TEST_RSSI, KISS_FEND});

  modem.onPacketReceived((int8_t)TEST_SNR, (int8_t)TEST_RSSI, packet.data(), packet.size());
  EXPECT_EQ(serial.writesSnapshot(), expected);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
