#include <gtest/gtest.h>
#include <array>
#include <string>
#include "../../variants/rak3401/OnboardRadioSleep.h"

namespace {
struct FakeRadioIO {
  std::string version = "SX1261";
  std::array<bool, 3> ready = {true, true, true};
  std::string calls;
  unsigned waits = 0;
  bool probePulls = true;

  void begin() { calls += "begin "; }
  void reset() { calls += "reset "; }
  bool waitBusy(bool high, unsigned timeout) {
    calls += high ? "busy-high " : "busy-low ";
    EXPECT_EQ(timeout, 20u);
    if (high) EXPECT_FALSE(probePulls);
    const bool result = ready.at(waits++);
    return result;
  }
  void readVersion(char* out) { calls += "version "; memcpy(out, version.data(), 6); }
  void releaseProbePulls() { calls += "release-pulls "; probePulls = false; }
  void sleep() { EXPECT_FALSE(probePulls); calls += "sleep "; }
  void end() { calls += "end "; }
};
}

TEST(RakOnboardRadio, PresentRadioSleepsBeforeBusRelease) {
  FakeRadioIO io;
  EXPECT_EQ(rak3401::sleepOnboardRadio(io, "SX1261"), rak3401::OnboardRadioState::Asleep);
  EXPECT_EQ(io.calls, "begin reset busy-low version release-pulls busy-low sleep busy-high end ");
}

TEST(RakOnboardRadio, AbsentAndFloatingResponsesNeverReceiveSleep) {
  for (const std::string version : {std::string(6, '\0'), std::string(6, '\xff'), std::string("SX126X")}) {
    FakeRadioIO io;
    io.version = version;
    EXPECT_EQ(rak3401::sleepOnboardRadio(io, "SX1261"), rak3401::OnboardRadioState::Absent);
    EXPECT_EQ(io.calls, "begin reset busy-low version end ");
  }
}

TEST(RakOnboardRadio, BusyTimeoutsReleaseBusWithoutSleep) {
  for (unsigned failure = 0; failure < 2; failure++) {
    FakeRadioIO io;
    io.ready[failure] = false;
    EXPECT_EQ(rak3401::sleepOnboardRadio(io, "SX1261"), rak3401::OnboardRadioState::BusyTimeout);
    EXPECT_EQ(io.calls.find("sleep "), std::string::npos);
    EXPECT_EQ(io.calls, failure == 0 ? "begin reset busy-low end " :
                                     "begin reset busy-low version release-pulls busy-low end ");
  }
}

TEST(RakOnboardRadio, MissingSleepReadbackIsReportedAndBusReleased) {
  FakeRadioIO io;
  io.ready[2] = false;
  EXPECT_EQ(rak3401::sleepOnboardRadio(io, "SX1261"), rak3401::OnboardRadioState::SleepFailed);
  EXPECT_EQ(io.calls.substr(io.calls.size() - 4), "end ");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
