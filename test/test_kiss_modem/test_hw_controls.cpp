#include <gtest/gtest.h>

#include <queue>
#include <vector>

#include "KissModem.h"

namespace {

class RecordingStream : public Stream {
public:
  void pushRx(const std::vector<uint8_t>& bytes) {
    for (uint8_t b : bytes) _rx.push(b);
  }

  int availableForWrite() override { return blocked ? 0 : 4096; }
  size_t write(const uint8_t* buffer, size_t size) override {
    if (blocked) return 0;
    _writes.insert(_writes.end(), buffer, buffer + size);
    return size;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  int available() override { return (int)_rx.size(); }
  int read() override {
    if (_rx.empty()) return -1;
    int b = _rx.front();
    _rx.pop();
    return b;
  }

  // Decoded SetHardware payloads (sub-command + data), consumed on read.
  std::vector<std::vector<uint8_t>> takeHardwareFrames() {
    std::vector<std::vector<uint8_t>> frames;
    std::vector<uint8_t> cur;
    bool in_frame = false, esc = false;
    for (uint8_t b : _writes) {
      if (b == KISS_FEND) {
        if (in_frame && cur.size() > 1 && cur[0] == KISS_CMD_SETHARDWARE) {
          frames.emplace_back(cur.begin() + 1, cur.end());
        }
        cur.clear();
        in_frame = true;
        continue;
      }
      if (b == KISS_FESC) { esc = true; continue; }
      if (esc) { b = (b == KISS_TFEND) ? KISS_FEND : KISS_FESC; esc = false; }
      cur.push_back(b);
    }
    _writes.clear();
    return frames;
  }

  bool blocked = false;

private:
  std::queue<uint8_t> _rx;
  std::vector<uint8_t> _writes;
};

class ZeroRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override { memset(dest, 0, sz); }
};

class AgcRadio : public mesh::Radio {
public:
  bool startSendRaw(const uint8_t*, uint16_t) override { return true; }
  bool isSendComplete() override { return send_complete; }
  void resetAGC() override { agc_resets++; }

  bool send_complete = true;
  int agc_resets = 0;
};

// Implements FEM through the "radio.fem.*" CLI text, worded like the Heltec / Station G3 boards.
class FemBoard : public mesh::MainBoard {
public:
  const char* getManufacturerName() override { return "fem-board"; }

  bool handleCommand(const char* command, uint32_t, char* reply) override {
    commands++;
    if (handleGain(command, reply, "rxgain", has_lna, lna, lna_sets, lna_sticks)) return true;
    if (handleGain(command, reply, "txgain", has_pa, pa, pa_sets, pa_sticks)) return true;
    return false;
  }

  bool has_lna = false, has_pa = false;
  bool lna = false, pa = false;
  int lna_sets = 0, pa_sets = 0;
  bool lna_sticks = false, pa_sticks = false;  // set reports OK but the state does not change
  bool unhandled = false;                       // board does not implement radio.fem.* at all
  const char* odd_get_reply = nullptr;          // unexpected get wording
  int commands = 0;

private:
  bool handleGain(const char* command, char* reply, const char* name, bool has, bool& state, int& sets, bool sticks) {
    if (unhandled) return false;
    char get_cmd[32], set_cmd[32];
    snprintf(get_cmd, sizeof(get_cmd), "get radio.fem.%s", name);
    snprintf(set_cmd, sizeof(set_cmd), "set radio.fem.%s ", name);
    if (strcmp(command, get_cmd) == 0) {
      if (!has) strcpy(reply, "Error: unsupported");
      else if (odd_get_reply) strcpy(reply, odd_get_reply);
      else sprintf(reply, "> %s", state ? "on" : "off");
      return true;
    }
    if (strncmp(command, set_cmd, strlen(set_cmd)) == 0) {
      if (!has) {
        strcpy(reply, "Error: unsupported");
      } else {
        sets++;
        if (!sticks) state = strcmp(command + strlen(set_cmd), "on") == 0;
        strcpy(reply, "OK - LoRa FEM gain changed");
      }
      return true;
    }
    return false;
  }
};

class NoSensors : public SensorManager {
public:
  bool querySensors(uint8_t, CayenneLPP&) override { return false; }
};

class KissHwControlTest : public ::testing::Test {
protected:
  RecordingStream serial;
  mesh::LocalIdentity identity;
  ZeroRNG rng;
  AgcRadio radio;
  FemBoard board;
  NoSensors sensors;
  KissModem modem;

  KissHwControlTest() : modem(serial, identity, rng, radio, board, sensors) {
    g_mock_millis = 1000;
    modem.begin();
  }

  std::vector<std::vector<uint8_t>> hw(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame = {KISS_FEND, KISS_CMD_SETHARDWARE};
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(KISS_FEND);
    serial.pushRx(frame);
    modem.loop();
    return serial.takeHardwareFrames();
  }

  std::vector<uint8_t> hw1(const std::vector<uint8_t>& payload) {
    auto frames = hw(payload);
    EXPECT_EQ(frames.size(), 1U);
    return frames.empty() ? std::vector<uint8_t>{} : frames[0];
  }

  void advance(uint32_t ms) {
    g_mock_millis += ms;
    modem.loop();
  }

  void startTxAndHold() {
    radio.send_complete = false;
    serial.pushRx({KISS_FEND, KISS_CMD_DATA, 0x42, KISS_FEND});
    modem.loop();
    modem.loop();
    advance((uint32_t)KISS_DEFAULT_TXDELAY * 10);
    ASSERT_TRUE(modem.isActuallyTransmitting());
  }
};

bool g_rx_boost = false;
bool g_rx_boost_set_ok = true;
int g_rx_boost_sets = 0;
bool fakeSetRxBoost(bool en) {
  g_rx_boost_sets++;
  if (!g_rx_boost_set_ok) return false;
  g_rx_boost = en;
  return true;
}
bool fakeGetRxBoost() { return g_rx_boost; }

KissModem* g_poll_modem = nullptr;
bool g_poll_has_packet = false;
int g_polls = 0;
void fakePollRx() {
  g_polls++;
  if (g_poll_has_packet && g_poll_modem) {
    static constexpr uint8_t PKT[] = {0x55};
    g_poll_has_packet = false;
    g_poll_modem->onPacketReceived(4, -80, PKT, sizeof(PKT));
  }
}

const std::vector<uint8_t> ERR_UNSUPPORTED = {HW_RESP_ERROR, HW_ERR_UNSUPPORTED};

TEST_F(KissHwControlTest, VersionIsTwo) {
  EXPECT_EQ(hw1({HW_CMD_GET_VERSION}), (std::vector<uint8_t>{HW_RESP(HW_CMD_GET_VERSION), 2, 0}));
}

TEST_F(KissHwControlTest, CapabilitiesOnBareBoardAreAgcOnly) {
  EXPECT_EQ(hw1({HW_CMD_GET_CAPABILITIES}), (std::vector<uint8_t>{0x9B, 0x01, 0x00, 0x00, 0x00}));
}

TEST_F(KissHwControlTest, CapabilitiesReflectBoardFem) {
  board.has_lna = true;
  EXPECT_EQ(hw1({HW_CMD_GET_CAPABILITIES}), (std::vector<uint8_t>{0x9B, 0x03, 0x00, 0x00, 0x00}));
  board.has_pa = true;
  EXPECT_EQ(hw1({HW_CMD_GET_CAPABILITIES}), (std::vector<uint8_t>{0x9B, 0x07, 0x00, 0x00, 0x00}));
}

TEST_F(KissHwControlTest, AgcIntervalDefaultsToThirtyTwoSeconds) {
  EXPECT_EQ(hw1({HW_CMD_GET_AGC_RESET_INTERVAL}), (std::vector<uint8_t>{0x9D, 32, 0}));
  // the default must be restorable, i.e. a multiple of the 4 s step
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, 4, 0}), (std::vector<uint8_t>{0x9D, 4, 0}));
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, KISS_AGC_RESET_DEFAULT_SEC, 0}), (std::vector<uint8_t>{0x9D, KISS_AGC_RESET_DEFAULT_SEC, 0}));
}

TEST_F(KissHwControlTest, AgcIntervalSetReturnsEffectiveRoundedValue) {
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, 10, 0}), (std::vector<uint8_t>{0x9D, 8, 0}));
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, 4, 0}), (std::vector<uint8_t>{0x9D, 4, 0}));
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, 0xFC, 0x03}), (std::vector<uint8_t>{0x9D, 0xFC, 0x03}));
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, 0, 0}), (std::vector<uint8_t>{0x9D, 0, 0}));
  EXPECT_EQ(hw1({HW_CMD_GET_AGC_RESET_INTERVAL}), (std::vector<uint8_t>{0x9D, 0, 0}));
}

TEST_F(KissHwControlTest, AgcIntervalRejectsOutOfRangeAndShortPayload) {
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, 0xFD, 0x03}), (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_INVALID_PARAM}));
  EXPECT_EQ(hw1({HW_CMD_SET_AGC_RESET_INTERVAL, 4}), (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_INVALID_LENGTH}));
  EXPECT_EQ(hw1({HW_CMD_GET_AGC_RESET_INTERVAL}), (std::vector<uint8_t>{0x9D, 32, 0}));
}

TEST_F(KissHwControlTest, AgcResetsOnDefaultSchedule) {
  advance(31999);
  EXPECT_EQ(radio.agc_resets, 0);
  advance(1);
  EXPECT_EQ(radio.agc_resets, 1);
  advance(31999);
  EXPECT_EQ(radio.agc_resets, 1);
  advance(1);
  EXPECT_EQ(radio.agc_resets, 2);
}

TEST_F(KissHwControlTest, AgcZeroDisablesResets) {
  hw({HW_CMD_SET_AGC_RESET_INTERVAL, 0, 0});
  for (int i = 0; i < 10; i++) advance(60000);
  EXPECT_EQ(radio.agc_resets, 0);
}

TEST_F(KissHwControlTest, AgcIntervalChangeRestartsDeadline) {
  hw({HW_CMD_SET_AGC_RESET_INTERVAL, 0x2C, 0x01});  // 300 s
  advance(100000);
  hw({HW_CMD_SET_AGC_RESET_INTERVAL, 4, 0});
  EXPECT_EQ(radio.agc_resets, 0);
  advance(3999);
  EXPECT_EQ(radio.agc_resets, 0);
  advance(1);
  EXPECT_EQ(radio.agc_resets, 1);
}

TEST_F(KissHwControlTest, AgcResetWaitsForTxToFinish) {
  hw({HW_CMD_SET_AGC_RESET_INTERVAL, 4, 0});
  startTxAndHold();
  advance(10000);
  EXPECT_EQ(radio.agc_resets, 0);
  radio.send_complete = true;
  modem.loop();  // TX done -> TX_DONE_PENDING
  modem.loop();  // TxDone flushed -> idle, overdue reset runs
  EXPECT_EQ(radio.agc_resets, 1);
}

TEST_F(KissHwControlTest, FemUnsupportedOnBareBoard) {
  EXPECT_EQ(hw1({HW_CMD_GET_FEM_STATE}), (std::vector<uint8_t>{0x9F, 0x00, 0x00}));
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}), ERR_UNSUPPORTED);
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_TX_GAIN, 0}), ERR_UNSUPPORTED);
}

TEST_F(KissHwControlTest, FemSetRxLeavesTxUntouched) {
  board.has_lna = board.has_pa = true;
  board.pa = true;
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}), (std::vector<uint8_t>{0x9F, 0x03, 0x03}));
  EXPECT_TRUE(board.lna);
  EXPECT_EQ(board.pa_sets, 0);

  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_TX_GAIN, 0}), (std::vector<uint8_t>{0x9F, 0x03, 0x01}));
  EXPECT_FALSE(board.pa);
  EXPECT_EQ(board.lna_sets, 1);
}

TEST_F(KissHwControlTest, FemPartialCapabilityRejectsWholeRequest) {
  board.has_lna = true;
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN | HW_FEM_TX_GAIN, HW_FEM_RX_GAIN | HW_FEM_TX_GAIN}), ERR_UNSUPPORTED);
  EXPECT_EQ(board.lna_sets, 0);
  EXPECT_EQ(hw1({HW_CMD_GET_FEM_STATE}), (std::vector<uint8_t>{0x9F, 0x01, 0x00}));
}

TEST_F(KissHwControlTest, FemUnknownBitsAreUnsupported) {
  board.has_lna = board.has_pa = true;
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, 0x04, 0x04}), ERR_UNSUPPORTED);
}

TEST_F(KissHwControlTest, FemUnhandledCommandsMeanUnsupported) {
  board.has_lna = board.has_pa = true;
  board.unhandled = true;
  EXPECT_EQ(hw1({HW_CMD_GET_FEM_STATE}), (std::vector<uint8_t>{0x9F, 0x00, 0x00}));
  EXPECT_EQ(hw1({HW_CMD_GET_CAPABILITIES}), (std::vector<uint8_t>{0x9B, 0x01, 0x00, 0x00, 0x00}));
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}), ERR_UNSUPPORTED);
}

TEST_F(KissHwControlTest, FemUnexpectedGetWordingMeansUnsupported) {
  board.has_lna = true;
  board.odd_get_reply = "> enabled";
  EXPECT_EQ(hw1({HW_CMD_GET_FEM_STATE}), (std::vector<uint8_t>{0x9F, 0x00, 0x00}));
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}), ERR_UNSUPPORTED);
  EXPECT_EQ(board.lna_sets, 0);
}

TEST_F(KissHwControlTest, FemSetThatDoesNotTakeRepliesWithReadBackState) {
  board.has_lna = board.has_pa = true;
  board.lna_sticks = true;  // board says OK but LNA stays off
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN | HW_FEM_TX_GAIN, HW_FEM_RX_GAIN | HW_FEM_TX_GAIN}),
            (std::vector<uint8_t>{0x9F, 0x03, 0x02}));
  EXPECT_FALSE(board.lna);
  EXPECT_TRUE(board.pa);
}

TEST_F(KissHwControlTest, FemShortPayloadIsInvalidLength) {
  EXPECT_EQ(hw1({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN}), (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_INVALID_LENGTH}));
}

TEST_F(KissHwControlTest, FemSetDuringTxIsDeferredUntilAfterTxDone) {
  board.has_lna = true;
  startTxAndHold();
  serial.takeHardwareFrames();

  EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}).empty());
  EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, 0}).empty());  // queued behind the first
  EXPECT_EQ(board.lna_sets, 0);

  radio.send_complete = true;
  modem.loop();  // TX done -> TX_DONE_PENDING; FEM still held
  modem.loop();  // TxDone queued -> idle; FEM applied in order
  for (int i = 0; i < 3; i++) modem.loop();
  auto frames = serial.takeHardwareFrames();
  ASSERT_EQ(frames.size(), 3U);
  EXPECT_EQ(frames[0], (std::vector<uint8_t>{HW_RESP_TX_DONE, 0x01}));
  EXPECT_EQ(frames[1], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
  EXPECT_EQ(frames[2], (std::vector<uint8_t>{0x9F, 0x01, 0x00}));
  EXPECT_FALSE(board.lna);
  EXPECT_EQ(board.lna_sets, 2);
}

TEST_F(KissHwControlTest, PipelinedFemRequestsAreAnsweredInArrivalOrder) {
  board.has_lna = true;
  startTxAndHold();
  serial.takeHardwareFrames();

  EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}).empty());
  EXPECT_TRUE(hw({HW_CMD_GET_FEM_STATE}).empty());
  EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_TX_GAIN, HW_FEM_TX_GAIN}).empty());  // unsupported bit
  EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN}).empty());                  // short payload

  radio.send_complete = true;
  for (int i = 0; i < 6; i++) modem.loop();
  auto frames = serial.takeHardwareFrames();
  ASSERT_EQ(frames.size(), 5U);
  EXPECT_EQ(frames[0], (std::vector<uint8_t>{HW_RESP_TX_DONE, 0x01}));
  EXPECT_EQ(frames[1], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
  EXPECT_EQ(frames[2], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
  EXPECT_EQ(frames[3], ERR_UNSUPPORTED);
  EXPECT_EQ(frames[4], (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_INVALID_LENGTH}));
  EXPECT_EQ(board.lna_sets, 1);
}

TEST_F(KissHwControlTest, FemQueueOverflowRepliesTxBusyImmediately) {
  board.has_lna = true;
  startTxAndHold();
  serial.takeHardwareFrames();
  for (int i = 0; i < KISS_FEM_OP_QUEUE_DEPTH; i++) {
    EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}).empty());
  }
  EXPECT_EQ(hw1({HW_CMD_GET_FEM_STATE}), (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_TX_BUSY}));
  EXPECT_EQ(board.lna_sets, 0);
}

TEST_F(KissHwControlTest, FemGetWithNothingQueuedIsAnsweredDuringTx) {
  board.has_lna = true;
  startTxAndHold();
  serial.takeHardwareFrames();
  EXPECT_EQ(hw1({HW_CMD_GET_FEM_STATE}), (std::vector<uint8_t>{0x9F, 0x01, 0x00}));
}

TEST_F(KissHwControlTest, FemGetDuringDeferredSetIsAnsweredAfterSet) {
  board.has_lna = true;
  startTxAndHold();
  serial.takeHardwareFrames();

  EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}).empty());
  EXPECT_TRUE(hw({HW_CMD_GET_FEM_STATE}).empty());

  radio.send_complete = true;
  modem.loop();
  modem.loop();
  auto frames = serial.takeHardwareFrames();
  ASSERT_EQ(frames.size(), 3U);
  EXPECT_EQ(frames[0], (std::vector<uint8_t>{HW_RESP_TX_DONE, 0x01}));
  EXPECT_EQ(frames[1], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
  EXPECT_EQ(frames[2], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
}

TEST_F(KissHwControlTest, DeferredFemRepliesSurviveHostBackpressure) {
  board.has_lna = true;
  hw({HW_CMD_SET_SIGNAL_REPORT, 0});  // one output frame per RX packet
  startTxAndHold();
  serial.takeHardwareFrames();
  EXPECT_TRUE(hw({HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN}).empty());
  EXPECT_TRUE(hw({HW_CMD_GET_FEM_STATE}).empty());

  // with writes blocked: RX packet takes slot 1, TxDone slot 2, so the Set reply cannot be queued
  serial.blocked = true;
  static constexpr uint8_t PKT[] = {0x01};
  modem.onPacketReceived(0, 0, PKT, sizeof(PKT));
  radio.send_complete = true;
  for (int i = 0; i < 5; i++) modem.loop();
  EXPECT_TRUE(board.lna);  // applied once TxDone was queued, reply still held

  serial.blocked = false;
  for (int i = 0; i < 5; i++) modem.loop();
  EXPECT_EQ(board.lna_sets, 1);

  auto frames = serial.takeHardwareFrames();
  ASSERT_EQ(frames.size(), 3U);
  EXPECT_EQ(frames[0], (std::vector<uint8_t>{HW_RESP_TX_DONE, 0x01}));
  EXPECT_EQ(frames[1], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
  EXPECT_EQ(frames[2], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
}

TEST_F(KissHwControlTest, ImmediateFemReplySurvivesHostBackpressure) {
  board.has_lna = true;
  hw({HW_CMD_SET_SIGNAL_REPORT, 0});

  serial.blocked = true;
  static constexpr uint8_t PKT[] = {0x01};
  modem.onPacketReceived(0, 0, PKT, sizeof(PKT));
  modem.onPacketReceived(0, 0, PKT, sizeof(PKT));  // output queue now full
  serial.pushRx({KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_SET_FEM_STATE, HW_FEM_RX_GAIN, HW_FEM_RX_GAIN, KISS_FEND});
  modem.loop();
  EXPECT_TRUE(board.lna);

  serial.blocked = false;
  for (int i = 0; i < 5; i++) modem.loop();
  auto frames = serial.takeHardwareFrames();
  ASSERT_EQ(frames.size(), 1U);  // FemState, not TxBusy
  EXPECT_EQ(frames[0], (std::vector<uint8_t>{0x9F, 0x01, 0x01}));
  EXPECT_EQ(board.lna_sets, 1);
}

class KissRxBoostTest : public KissHwControlTest {
protected:
  KissRxBoostTest() {
    g_rx_boost = true;
    g_rx_boost_set_ok = true;
    g_rx_boost_sets = 0;
    modem.setRxBoostedGainCallbacks(fakeSetRxBoost, fakeGetRxBoost);
    g_poll_modem = &modem;
    g_poll_has_packet = false;
    g_polls = 0;
    modem.setPollRxCallback(fakePollRx);
  }
};

TEST_F(KissHwControlTest, RxBoostedGainUnsupportedWithoutCallbacks) {
  EXPECT_EQ(hw1({HW_CMD_GET_RX_BOOSTED_GAIN}), ERR_UNSUPPORTED);
  EXPECT_EQ(hw1({HW_CMD_SET_RX_BOOSTED_GAIN, 1}), ERR_UNSUPPORTED);
}

TEST_F(KissRxBoostTest, CapabilityAdvertised) {
  EXPECT_EQ(hw1({HW_CMD_GET_CAPABILITIES}), (std::vector<uint8_t>{0x9B, 0x09, 0x00, 0x00, 0x00}));
}

TEST_F(KissRxBoostTest, GetReportsCurrentState) {
  EXPECT_EQ(hw1({HW_CMD_GET_RX_BOOSTED_GAIN}), (std::vector<uint8_t>{0xA1, 0x01}));
}

TEST_F(KissRxBoostTest, SetRepliesWithEffectiveState) {
  EXPECT_EQ(hw1({HW_CMD_SET_RX_BOOSTED_GAIN, 0}), (std::vector<uint8_t>{0xA1, 0x00}));
  EXPECT_FALSE(g_rx_boost);
  EXPECT_EQ(hw1({HW_CMD_SET_RX_BOOSTED_GAIN, 0x7F}), (std::vector<uint8_t>{0xA1, 0x01}));
  EXPECT_TRUE(g_rx_boost);
}

TEST_F(KissRxBoostTest, SetFailureRepliesWithUnchangedState) {
  g_rx_boost_set_ok = false;
  EXPECT_EQ(hw1({HW_CMD_SET_RX_BOOSTED_GAIN, 0}), (std::vector<uint8_t>{0xA1, 0x01}));
  EXPECT_TRUE(g_rx_boost);
}

TEST_F(KissRxBoostTest, SetDuringTxIsRejectedWithoutTouchingRadio) {
  startTxAndHold();
  serial.takeHardwareFrames();
  EXPECT_EQ(hw1({HW_CMD_SET_RX_BOOSTED_GAIN, 0}), (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_TX_BUSY}));
  EXPECT_EQ(g_rx_boost_sets, 0);
  EXPECT_EQ(hw1({HW_CMD_GET_RX_BOOSTED_GAIN}), (std::vector<uint8_t>{0xA1, 0x01}));
}

TEST_F(KissRxBoostTest, SetShortPayloadIsInvalidLength) {
  EXPECT_EQ(hw1({HW_CMD_SET_RX_BOOSTED_GAIN}), (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_INVALID_LENGTH}));
  EXPECT_EQ(g_rx_boost_sets, 0);
}

TEST_F(KissRxBoostTest, CompletedRxPacketIsDeliveredBeforeGainChange) {
  g_poll_has_packet = true;
  auto frames = hw({HW_CMD_SET_RX_BOOSTED_GAIN, 0});
  EXPECT_EQ(g_polls, 1);
  EXPECT_FALSE(g_rx_boost);
  ASSERT_EQ(frames.size(), 2U);  // RxMeta for the drained packet, then the gain reply
  EXPECT_EQ(frames[0][0], HW_RESP_RX_META);
  EXPECT_EQ(frames[1], (std::vector<uint8_t>{0xA1, 0x00}));
}

TEST_F(KissRxBoostTest, NoRoomForReplyAfterDrainChangesNothing) {
  serial.blocked = true;
  g_poll_has_packet = true;  // data + meta fill both output slots
  serial.pushRx({KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_SET_RX_BOOSTED_GAIN, 0, KISS_FEND});
  modem.loop();
  EXPECT_EQ(g_rx_boost_sets, 0);
  EXPECT_TRUE(g_rx_boost);

  serial.blocked = false;
  for (int i = 0; i < 5; i++) modem.loop();
  auto frames = serial.takeHardwareFrames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0][0], HW_RESP_RX_META);
  EXPECT_EQ(frames[1], (std::vector<uint8_t>{HW_RESP_ERROR, HW_ERR_TX_BUSY}));
}

TEST_F(KissRxBoostTest, SetWhileHostOutputBackedUpIsRejected) {
  serial.blocked = true;
  static constexpr uint8_t PKT[] = {0x01};
  modem.onPacketReceived(0, 0, PKT, sizeof(PKT));
  serial.pushRx({KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_SET_RX_BOOSTED_GAIN, 0, KISS_FEND});
  modem.loop();
  EXPECT_EQ(g_polls, 0);
  EXPECT_EQ(g_rx_boost_sets, 0);
}

}  // namespace
