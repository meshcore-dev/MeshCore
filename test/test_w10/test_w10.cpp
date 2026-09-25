#include <gtest/gtest.h>
#include <MCP23017.h>
#include <MeshnologyW10Hal.h>
#include <MeshnologyW10RTC.h>
#include <MeshnologyW10Radio.h>

TEST(W10Expander, OutputUpdatePreservesRadioResetAndUsesLatchNotInputs) {
  TwoWire wire;
  MCP23017 io(wire);
  wire.registers[0x14] = 0x08; // radio NRST already high
  wire.registers[0x12] = 0xFF; // unrelated input levels must not be written to OLAT
  ASSERT_TRUE(io.digitalWrite(1, HIGH));
  EXPECT_EQ(wire.registers[0x14], 0x0A);
  ASSERT_TRUE(io.digitalWrite(1, LOW));
  EXPECT_EQ(wire.registers[0x14], 0x08);
}

TEST(W10Expander, NackAndShortReadCannotClobberOutputBank) {
  for (bool short_read : {false, true}) {
    TwoWire wire;
    MCP23017 io(wire);
    wire.registers[0x14] = 0xAA;
    wire.nack = !short_read;
    wire.short_read = short_read;
    EXPECT_FALSE(io.digitalWrite(3, LOW));
    EXPECT_EQ(wire.registers[0x14], 0xAA);
    EXPECT_EQ(wire.writes, 0U);
    EXPECT_EQ(io.getErrorCount(), 1U);
  }
}

TEST(W10Expander, FailedWriteIsReportedAndCanBeRetried) {
  TwoWire wire;
  MCP23017 io(wire);
  wire.fail_write = true;
  EXPECT_FALSE(io.digitalWrite(12, HIGH));
  EXPECT_EQ(io.getErrorCount(), 1U);
  wire.fail_write = false;
  EXPECT_TRUE(io.digitalWrite(12, HIGH));
  EXPECT_EQ(wire.registers[0x15], 0x10);
}

TEST(W10Expander, DirectionsAndPullupsAddressTheCorrectBank) {
  TwoWire wire;
  MCP23017 io(wire);
  wire.registers[0x01] = 0xFF;
  ASSERT_TRUE(io.pinMode(12, OUTPUT));
  EXPECT_EQ(wire.registers[0x01], 0xEF);
  ASSERT_TRUE(io.pinMode(12, INPUT_PULLUP));
  EXPECT_EQ(wire.registers[0x01], 0xFF);
  EXPECT_EQ(wire.registers[0x0D], 0x10);
  ASSERT_TRUE(io.pinMode(12, INPUT));
  EXPECT_EQ(wire.registers[0x0D], 0);
  EXPECT_FALSE(io.pinMode(16, OUTPUT));
  EXPECT_FALSE(io.digitalWrite(255, HIGH));
}

TEST(W10Hal, VirtualPinsNeverReachNativeGPIOOrInterruptAPIs) {
  TwoWire wire;
  MCP23017 io(wire);
  SPIClass spi;
  MeshnologyW10Hal hal(spi, io);
  hal.pinMode(103, OUTPUT);
  hal.digitalWrite(103, HIGH);
  hal.digitalRead(110);
  EXPECT_EQ(hal.pinToInterrupt(109), RADIOLIB_NC);
  hal.attachInterrupt(hal.pinToInterrupt(109), nullptr, 1);
  hal.detachInterrupt(hal.pinToInterrupt(109));
  hal.attachInterrupt(109, nullptr, 1);
  EXPECT_EQ(hal.native_calls, 0U);
  hal.digitalWrite(14, HIGH);
  EXPECT_EQ(hal.native_calls, 1U);
  EXPECT_EQ(hal.last_pin, 14U);
}

TEST(W10Hal, FailedReadsKeepBusyHighAndCompletionLow) {
  TwoWire wire;
  MCP23017 io(wire);
  SPIClass spi;
  MeshnologyW10Hal hal(spi, io);
  wire.nack = true;
  EXPECT_EQ(hal.digitalRead(110), HIGH);
  EXPECT_EQ(hal.digitalRead(109), LOW);
  wire.nack = false;
  wire.registers[0x13] = 0x02;
  EXPECT_EQ(hal.digitalRead(110), LOW);
  EXPECT_EQ(hal.digitalRead(109), HIGH);
}

class W10RadioTest : public ::testing::Test {
protected:
  class Board : public mesh::MainBoard {
  public:
    uint16_t getBattMilliVolts() override { return 4200; }
    const char* getManufacturerName() const override { return "test"; }
    void reboot() override { }
    uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
  } board;
  TwoWire wire;
  MCP23017 io{wire};
  CustomSX1262 chip;
  MeshnologyW10Radio radio{chip, board, io};

  void SetUp() override {
    g_mock_millis = 0;
    radio.begin();
    radio.setCADEnabled(true);
  }
};

TEST_F(W10RadioTest, LateCompletionAfterFailedSendDoesNotBecomeAPacket) {
  uint8_t bytes[8]{};
  ASSERT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  chip.tx_error = -1;
  ASSERT_FALSE(radio.startSendRaw(bytes, 3));
  ASSERT_FALSE(radio.isInRecvMode());
  wire.registers[0x13] = 0x02; // DIO1 asserts after the failed send.
  EXPECT_FALSE(radio.isSendComplete());
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_EQ(chip.reads, 0);
  EXPECT_EQ(radio.getPacketsRecv(), 0U);
  EXPECT_EQ(radio.getPacketsSent(), 0U);
  EXPECT_TRUE(radio.isInRecvMode());
  EXPECT_EQ(chip.rx_starts, 2);
  wire.registers[0x13] = 0; // startReceive clears the hardware IRQ.
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  wire.registers[0x13] = 0x02; // A new packet completes in RX.
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 3);
  EXPECT_EQ(chip.reads, 1);
  EXPECT_EQ(radio.getPacketsRecv(), 1U);
}

TEST_F(W10RadioTest, CADReadFailuresReturnBusyAndReceptionCanRecover) {
  for (bool short_read : {false, true}) {
    wire.nack = !short_read;
    wire.short_read = short_read;
    EXPECT_TRUE(radio.isChannelActive());
    EXPECT_EQ(chip.scan_results, 0U);
    EXPECT_LT(g_mock_millis, 5000U);
    wire.nack = wire.short_read = false;
    wire.registers[0x13] = 0x02; // DIO1: scan completed
    EXPECT_FALSE(radio.isChannelActive());
    EXPECT_TRUE(radio.isInRecvMode());
    wire.registers[0x13] = 0;
    chip.scan_results = 0;
  }
}

TEST_F(W10RadioTest, MissingCADCompletionTimesOutAcrossMillisWrap) {
  for (uint32_t start : {0U, UINT32_MAX - 100U}) {
    g_mock_millis = start;
    EXPECT_TRUE(radio.isChannelActive());
    EXPECT_EQ(uint32_t(g_mock_millis - start), 5000U);
    EXPECT_EQ(chip.scan_results, 0U);
    EXPECT_TRUE(radio.isInRecvMode());
  }
}

TEST_F(W10RadioTest, FailedCADStartReturnsBusyWithoutReadingResult) {
  chip.scan_start_error = RADIOLIB_ERR_SPI_CMD_FAILED;
  EXPECT_TRUE(radio.isChannelActive());
  EXPECT_EQ(chip.scan_results, 0U);
  EXPECT_EQ(g_mock_millis, 0U);
  EXPECT_TRUE(radio.isInRecvMode());
}

TEST_F(W10RadioTest, CompletedCADPreservesResultAndDoesNotDeliverAPacket) {
  wire.registers[0x13] = 0x02;
  for (int16_t result : {RADIOLIB_CHANNEL_FREE, RADIOLIB_LORA_DETECTED,
                         RADIOLIB_ERR_SPI_CMD_FAILED}) {
    chip.scan_result = result;
    EXPECT_EQ(radio.isChannelActive(), result != RADIOLIB_CHANNEL_FREE);
    EXPECT_TRUE(radio.isInRecvMode());
  }
  EXPECT_EQ(chip.scan_results, 3U);
  wire.registers[0x13] = 0; // startReceive clears the hardware IRQ
  uint8_t bytes[8];
  EXPECT_EQ(radio.recvRaw(bytes, sizeof(bytes)), 0);
  EXPECT_EQ(chip.reads, 0);
}

class TestClock : public mesh::RTCClock {
public:
  uint32_t time = 1234;
  uint32_t getCurrentTime() override { return time; }
  void setCurrentTime(uint32_t value) override { time = value; }
};

static void setRtcRegisters(TwoWire& wire) {
  // 2024-02-29 12:34:56 UTC, Thursday
  const uint8_t data[] = {0x56, 0x34, 0x12, 0x29, 0x04, 0x02, 0x24};
  std::copy(std::begin(data), std::end(data), wire.registers.begin() + 4);
}

TEST(W10RTC, SeedsSystemClockFromValidPCF85063Time) {
  TwoWire wire;
  TestClock clock;
  MeshnologyW10RTC rtc(clock, wire);
  setRtcRegisters(wire);
  ASSERT_TRUE(rtc.begin());
  EXPECT_EQ(rtc.getCurrentTime(), 1709210096U);
  EXPECT_EQ(wire.writes, 0U);
}

TEST(W10RTC, RejectsLostPowerStoppedClockAndInvalidCalendar) {
  for (int fault = 0; fault < 8; fault++) {
    TwoWire wire;
    TestClock clock;
    MeshnologyW10RTC rtc(clock, wire);
    setRtcRegisters(wire);
    switch (fault) {
      case 0: wire.registers[4] |= 0x80; break;
      case 1: wire.registers[0] = 0x20; break;
      case 2: wire.registers[0] = 0x02; break;
      case 3: wire.registers[7] = 0x30; break; // February 30
      case 4: wire.registers[5] = 0x1A; break; // Invalid BCD
      case 5: wire.nack = true; break;
      case 6: wire.short_read = true; break;
      case 7: wire.registers[10] = 0x23; break; // February 29 in a non-leap year
    }
    EXPECT_FALSE(rtc.begin());
    EXPECT_EQ(rtc.getCurrentTime(), 1234U);
  }
}

TEST(W10RTC, AcceptsSupportedCalendarEndpoints) {
  const uint8_t dates[][7] = {
    {0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x00}, // 2000-01-01
    {0x59, 0x59, 0x23, 0x31, 0x04, 0x12, 0x99}, // 2099-12-31
  };
  const uint32_t expected[] = {946684800U, 4102444799U};
  for (unsigned i = 0; i < 2; ++i) {
    TwoWire wire;
    TestClock clock;
    MeshnologyW10RTC rtc(clock, wire);
    std::copy(std::begin(dates[i]), std::end(dates[i]), wire.registers.begin() + 4);
    ASSERT_TRUE(rtc.begin());
    EXPECT_EQ(clock.time, expected[i]);
  }
}

TEST(W10RTC, RejectsOutOfRangeFieldsBeforeCallingCalendarLibrary) {
  const uint8_t invalid[][2] = {
    {4, 0x60}, {4, 0x79}, // seconds
    {5, 0x60}, {5, 0x79}, // minutes
    {6, 0x24}, {6, 0x39}, // hours
    {7, 0x00}, {7, 0x32}, {7, 0x39}, // day
    {8, 0x07}, // weekday
    {9, 0x00}, {9, 0x13}, {9, 0x19}, // month -- 0x19 would be a table overrun in RTCLib
  };
  for (const auto& field : invalid) {
    SCOPED_TRACE(::testing::Message() << "register=" << unsigned(field[0])
                                   << " value=" << unsigned(field[1]));
    TwoWire wire;
    TestClock clock;
    MeshnologyW10RTC rtc(clock, wire);
    setRtcRegisters(wire);
    wire.registers[field[0]] = field[1];
    const unsigned constructions = DateTime::component_constructions;
    EXPECT_FALSE(rtc.begin());
    EXPECT_EQ(DateTime::component_constructions, constructions);
    EXPECT_EQ(clock.time, 1234U);
    EXPECT_EQ(wire.writes, 0U);
  }
}

TEST(W10RTC, SettingTimeClearsLostPowerAndRestartsIn24HourMode) {
  TwoWire wire;
  TestClock clock;
  MeshnologyW10RTC rtc(clock, wire);
  setRtcRegisters(wire);
  wire.registers[0] = 0x02;
  wire.registers[4] = 0x80;
  EXPECT_FALSE(rtc.begin());
  rtc.setCurrentTime(1709210096U);
  EXPECT_EQ(clock.time, 1709210096U);
  EXPECT_EQ(wire.registers[0] & 0x22, 0);
  const uint8_t expected[] = {0x56, 0x34, 0x12, 0x29, 0x04, 0x02, 0x24};
  for (unsigned i = 0; i < sizeof(expected); i++) EXPECT_EQ(wire.registers[4+i], expected[i]);
}

TEST(W10RTC, FailedTimeWriteKeepsSoftwareTimeAndMarksHardwareTimeUnusable) {
  TwoWire wire;
  TestClock clock;
  MeshnologyW10RTC rtc(clock, wire);
  setRtcRegisters(wire);
  ASSERT_TRUE(rtc.begin());
  wire.fail_write_register = 4;
  rtc.setCurrentTime(1789510000U);
  EXPECT_EQ(rtc.getCurrentTime(), 1789510000U);
  EXPECT_EQ(wire.registers[0] & 0x20, 0x20);
  wire.fail_write_register = -1;
  TestClock next_clock;
  MeshnologyW10RTC next_boot(next_clock, wire);
  EXPECT_FALSE(next_boot.begin());
  EXPECT_EQ(next_clock.time, 1234U);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
