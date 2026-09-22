#include <gtest/gtest.h>
#include <Arduino.h>
#include <string>
#include <vector>
#include "../../variants/meshtracker_x1/MeshTrackerX1Flash.h"

namespace {
struct Flash {
  int pendingTransfers = 0;
  int busyReads = 0;
  bool readFails = false;
  bool stuckBusy = false;
  std::vector<std::string> events;
  bool transferIdle() {
    if (pendingTransfers > 0) { --pendingTransfers; return false; }
    return true;
  }
  bool readStatus(uint8_t& status) {
    events.push_back("status");
    if (readFails) return false;
    status = stuckBusy || busyReads-- > 0 ? 1 : 0;
    return true;
  }
  void unmount() { events.push_back("unmount"); }
  void stopBus() { events.push_back("stop"); }
  void releasePins() { events.push_back("release"); }
  void disablePower() { events.push_back("power-off"); }
};

TEST(X1Flash, WaitsForWriteCompletionBeforeUnmountAndPowerCut) {
  g_mock_millis = 0;
  Flash flash;
  flash.busyReads = 2;
  ASSERT_TRUE(shutdownX1Flash(flash));
  EXPECT_EQ(millis(), 2u);
  EXPECT_EQ(flash.events, (std::vector<std::string>{
    "status", "status", "status", "unmount", "stop", "release", "power-off"}));
}

TEST(X1Flash, DoesNotIssueStatusCommandDuringPendingTransfer) {
  g_mock_millis = 0;
  Flash flash;
  flash.pendingTransfers = 3;
  ASSERT_TRUE(shutdownX1Flash(flash));
  EXPECT_EQ(millis(), 3u);
  EXPECT_EQ(flash.events, (std::vector<std::string>{
    "status", "unmount", "stop", "release", "power-off"}));
}

TEST(X1Flash, StatusReadFailureLeavesFilesystemAndPowerIntact) {
  g_mock_millis = 0;
  Flash flash;
  flash.readFails = true;
  EXPECT_FALSE(shutdownX1Flash(flash));
  EXPECT_EQ(flash.events, (std::vector<std::string>{"status"}));
}

TEST(X1Flash, BusyTimeoutLeavesFilesystemAndPowerIntact) {
  g_mock_millis = 0;
  Flash flash;
  flash.stuckBusy = true;
  EXPECT_FALSE(shutdownX1Flash(flash));
  EXPECT_EQ(millis(), 5000u);
  for (const auto& event : flash.events) EXPECT_EQ(event, "status");
}

TEST(X1Flash, PendingTransferTimeoutDoesNotUnmountOrCutPower) {
  g_mock_millis = 0;
  Flash flash;
  flash.pendingTransfers = 6000;
  EXPECT_FALSE(shutdownX1Flash(flash));
  EXPECT_EQ(millis(), 5000u);
  EXPECT_TRUE(flash.events.empty());
}

TEST(X1Flash, BusyTimeoutSurvivesClockWrap) {
  g_mock_millis = UINT32_MAX - 10;
  Flash flash;
  flash.stuckBusy = true;
  EXPECT_FALSE(shutdownX1Flash(flash));
  EXPECT_EQ(millis(), 4989u);
  for (const auto& event : flash.events) EXPECT_EQ(event, "status");
}
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
