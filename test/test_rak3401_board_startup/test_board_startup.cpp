#include <gtest/gtest.h>
#include <array>
// Exercise the real startup code with a power-management stub that stops at SYSTEMOFF.
#include "../../variants/rak3401/RAK3401Board.cpp"

namespace startup_test {
std::vector<std::string> events;
bool permitBoot;
std::array<uint32_t, 48> outputs;
rak3401::OnboardRadioState radioState;
}

void pinMode(uint32_t, uint32_t) {}
void digitalWrite(uint32_t pin, uint32_t value) { startup_test::outputs.at(pin) = value; }
void analogReadResolution(uint32_t) {}
uint32_t analogRead(uint32_t) { return 2500; }

rak3401::OnboardRadioState rak3401::sleepOnboardRadio() {
  startup_test::events.push_back("onboard");
  return startup_test::radioState;
}

class RakBoardStartup : public testing::Test {
protected:
  void SetUp() override {
    startup_test::events.clear();
    startup_test::outputs.fill(HIGH);
    startup_test::permitBoot = true;
    startup_test::radioState = rak3401::OnboardRadioState::Asleep;
  }
};

TEST_F(RakBoardStartup, OnboardRadioHandledBeforeVoltageCheck) {
  RAK3401Board board;
  board.begin();
  EXPECT_EQ(startup_test::events, (std::vector<std::string>{"onboard", "voltage"}));
  EXPECT_EQ(startup_test::outputs.at(SX126X_POWER_EN), HIGH);
  EXPECT_EQ(startup_test::outputs.at(PIN_3V3_EN), HIGH);
}

TEST_F(RakBoardStartup, LowVoltageExitStillHandlesOnboardRadio) {
  startup_test::permitBoot = false;
  RAK3401Board board;
  try {
    board.begin();
    FAIL() << "Expected protective shutdown";
  } catch (const startup_test::SystemOff& off) {
    EXPECT_EQ(off.reason, SHUTDOWN_REASON_BOOT_PROTECT);
  }
  EXPECT_EQ(startup_test::events, (std::vector<std::string>{"onboard", "voltage", "off"}));
  EXPECT_EQ(startup_test::outputs.at(SX126X_POWER_EN), LOW);
  EXPECT_EQ(startup_test::outputs.at(PIN_3V3_EN), LOW);
}

TEST_F(RakBoardStartup, OptionalRadioFailuresDoNotBypassBootProtection) {
  for (auto state : {rak3401::OnboardRadioState::Absent, rak3401::OnboardRadioState::BusyTimeout,
                     rak3401::OnboardRadioState::SleepFailed}) {
    startup_test::events.clear();
    startup_test::permitBoot = false;
    startup_test::radioState = state;
    RAK3401Board board;
    EXPECT_THROW(board.begin(), startup_test::SystemOff);
    EXPECT_EQ(startup_test::events, (std::vector<std::string>{"onboard", "voltage", "off"}));
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
