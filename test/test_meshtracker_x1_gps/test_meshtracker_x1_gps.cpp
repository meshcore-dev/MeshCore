#include <gtest/gtest.h>
#include <Arduino.h>
#include <array>
#include <string>
#include <vector>

// Hardware boundary only; the production X1 sequencing code is tested below.
namespace {
constexpr int LOW = 0, HIGH = 1;
constexpr int GPS_RESET = 8, GPS_RTC_INT = 29, GPS_SLEEP_INT = 30;
constexpr int GPS_EN = 43, GPS_VRTC_EN = 45;
struct PinEvent { int pin, value; uint32_t at; };
std::array<int, 48> pins;
std::vector<PinEvent> events;
void digitalWrite(int pin, int value) {
  pins[pin] = value;
  events.push_back({pin, value, millis()});
}
}

#include "../../variants/meshtracker_x1/MeshTrackerX1GPS.h"

namespace {
class TestSerial : public Stream {
public:
  struct Command { std::string text; uint32_t at; };
  std::vector<Command> commands;
  int buffered = 0;
  int available() override { return buffered; }
  int read() override { return buffered > 0 ? (--buffered, 'x') : -1; }
  size_t write(const uint8_t* data, size_t size) override {
    commands.push_back({std::string(reinterpret_cast<const char*>(data), size), millis()});
    return size;
  }
};

class X1GPS : public testing::Test {
protected:
  TestSerial serial;
  MeshTrackerX1GPS gps{serial};
  void SetUp() override {
    g_mock_millis = 0;
    pins.fill(LOW);
    // Board startup configures these before the GPS controller runs.
    pins[GPS_VRTC_EN] = HIGH;
    pins[GPS_SLEEP_INT] = HIGH;
    events.clear();
  }
};

TEST_F(X1GPS, WakeUsesRtcPulseWithoutHardwareResetAndDiscardsOldBytes) {
  serial.buffered = 80;
  ASSERT_TRUE(gps.start());
  EXPECT_TRUE(gps.isActive());
  EXPECT_EQ(serial.buffered, 0);
  EXPECT_EQ(millis(), 103u);
  EXPECT_EQ(pins[GPS_EN], HIGH);
  EXPECT_EQ(pins[GPS_VRTC_EN], HIGH);
  EXPECT_EQ(pins[GPS_SLEEP_INT], HIGH);
  std::vector<PinEvent> pulse;
  for (const auto& event : events) {
    EXPECT_TRUE(event.pin == GPS_EN || event.pin == GPS_RTC_INT);
    if (event.pin == GPS_RTC_INT) pulse.push_back(event);
  }
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].pin, GPS_EN);
  EXPECT_EQ(events[0].value, HIGH);
  EXPECT_EQ(events[0].at, 0u);
  ASSERT_EQ(pulse.size(), 2u);
  EXPECT_EQ(pulse[0].value, HIGH);
  EXPECT_EQ(pulse[0].at, 50u);
  EXPECT_EQ(pulse[1].value, LOW);
  EXPECT_EQ(pulse[1].at, 53u);
  EXPECT_EQ(pins[GPS_RESET], LOW);
}

TEST_F(X1GPS, RepeatedStartDoesNotInterruptExistingFix) {
  gps.start();
  events.clear();
  serial.buffered = 12;
  EXPECT_FALSE(gps.start());
  EXPECT_TRUE(events.empty());
  EXPECT_EQ(serial.buffered, 12);
  EXPECT_EQ(millis(), 103u);
}

TEST_F(X1GPS, SteadyStateLoopDoesNotRepeatEnableOrPowerWrites) {
  gps.start();
  events.clear();
  for (int i = 0; i < 100; ++i) { delay(1000); gps.loop(); }
  EXPECT_TRUE(events.empty());
  EXPECT_TRUE(serial.commands.empty());
  gps.shutdown();
  events.clear();
  const auto commands = serial.commands.size();
  for (int i = 0; i < 100; ++i) { delay(1000); gps.loop(); }
  EXPECT_TRUE(events.empty());
  EXPECT_EQ(serial.commands.size(), commands);
}

TEST_F(X1GPS, SleepIsNonblockingAndKeepsPowerUntilAllCommandsFinish) {
  gps.start();
  const uint32_t start = millis();
  gps.requestSleep();
  EXPECT_FALSE(gps.isActive());
  EXPECT_EQ(millis(), start);
  ASSERT_EQ(serial.commands.size(), 1u);
  for (int i = 1; i <= 25; ++i) {
    delay(39);
    gps.loop();
    EXPECT_EQ(serial.commands.size(), static_cast<size_t>(i));
    EXPECT_EQ(pins[GPS_EN], HIGH);
    delay(1);
    gps.loop();
  }
  ASSERT_EQ(serial.commands.size(), 25u);
  for (size_t i = 0; i < serial.commands.size(); ++i) {
    EXPECT_EQ(serial.commands[i].text, "$PAIR650,0*25\r\n");
    EXPECT_EQ(serial.commands[i].at, start + 40 * i);
  }
  EXPECT_EQ(pins[GPS_EN], LOW);
  EXPECT_EQ(pins[GPS_VRTC_EN], HIGH);
  EXPECT_EQ(millis() - start, 1000u);
}

TEST_F(X1GPS, RepeatedSleepDoesNotRestartSequence) {
  gps.start();
  gps.requestSleep();
  delay(40);
  gps.loop();
  gps.requestSleep();
  gps.shutdown();
  EXPECT_EQ(serial.commands.size(), 25u);
  EXPECT_EQ(millis(), 1103u);
  EXPECT_EQ(pins[GPS_EN], LOW);
}

TEST_F(X1GPS, WakeCancelsPendingPowerCutEvenAfterLastSleepCommand) {
  gps.start();
  gps.requestSleep();
  for (int i = 1; i < 25; ++i) {
    delay(40);
    gps.loop();
  }
  ASSERT_EQ(serial.commands.size(), 25u);
  EXPECT_TRUE(gps.start());
  delay(2000);
  gps.loop();
  EXPECT_TRUE(gps.isActive());
  EXPECT_EQ(pins[GPS_EN], HIGH);
  EXPECT_EQ(serial.commands.size(), 25u);
  gps.requestSleep();
  gps.shutdown();
  EXPECT_EQ(serial.commands.size(), 50u);
  EXPECT_EQ(pins[GPS_EN], LOW);
}

TEST_F(X1GPS, DelayedLoopDoesNotBurstCommands) {
  gps.start();
  gps.requestSleep();
  delay(500);
  gps.loop();
  gps.loop();
  EXPECT_EQ(serial.commands.size(), 2u);
  EXPECT_EQ(pins[GPS_EN], HIGH);
  gps.shutdown();
  EXPECT_EQ(serial.commands.size(), 25u);
  EXPECT_EQ(pins[GPS_EN], LOW);
}

TEST_F(X1GPS, SleepTimingSurvivesMillisWrap) {
  gps.start();
  g_mock_millis = UINT32_MAX - 20;
  gps.requestSleep();
  gps.shutdown();
  EXPECT_EQ(serial.commands.size(), 25u);
  EXPECT_EQ(pins[GPS_EN], LOW);
  EXPECT_EQ(millis(), 979u);
}

TEST_F(X1GPS, DisabledGpsDoesNotSendCommandsOrWaitDuringShutdown) {
  gps.requestSleep();
  gps.shutdown();
  EXPECT_EQ(millis(), 0u);
  EXPECT_TRUE(serial.commands.empty());
  EXPECT_TRUE(events.empty());
  EXPECT_EQ(pins[GPS_VRTC_EN], HIGH);
}

TEST_F(X1GPS, ControllerShutdownAndWakePreserveBackupPowerAndReset) {
  gps.start();
  events.clear();
  gps.shutdown();
  EXPECT_EQ(serial.commands.size(), 25u);
  EXPECT_EQ(pins[GPS_EN], LOW);
  EXPECT_EQ(pins[GPS_VRTC_EN], HIGH);
  EXPECT_EQ(pins[GPS_RESET], LOW);
  EXPECT_TRUE(gps.start());
  EXPECT_EQ(pins[GPS_EN], HIGH);
  EXPECT_EQ(pins[GPS_VRTC_EN], HIGH);
  for (const auto& event : events) {
    EXPECT_NE(event.pin, GPS_VRTC_EN);
    EXPECT_NE(event.pin, GPS_RESET);
    EXPECT_NE(event.pin, GPS_SLEEP_INT);
  }
}
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
