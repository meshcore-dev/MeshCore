#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "helpers/MQTTLifecycle.h"

// Phase 4 teardown-focused test seam for the MQTT bridge lifecycle. Every case
// below maps to a bullet in STABILITY_TESTABILITY_HANDOFF.md's "Required
// teardown-focused tests" list or its "OTA Teardown Barrier" scenarios. The
// behavior encoded here is derived from the current MQTTBridge control flow
// (Phase 0 "derive from code + flag" discipline); it is the contract Phase 5
// must preserve when it wires the real bridge into MQTTLifecycle::Coordinator.

namespace L = MQTTLifecycle;

namespace {

// Recording Ops double with a settable clock and an ordered call log.
struct FakeOps : public L::Ops {
  uint32_t now = 0;
  int start_task_calls = 0;
  int deliver_stop_calls = 0;
  int release_calls = 0;
  int stop_complete_calls = 0;
  bool last_stop_clean = false;
  std::vector<std::string> log;

  uint32_t nowMs() override { return now; }
  void startTask() override {
    start_task_calls++;
    log.push_back("startTask");
  }
  void deliverStop() override {
    deliver_stop_calls++;
    log.push_back("deliverStop");
  }
  void releaseResources() override {
    release_calls++;
    log.push_back("release");
  }
  void onStopComplete(bool clean) override {
    stop_complete_calls++;
    last_stop_clean = clean;
    log.push_back(clean ? "otaClean" : "otaDirty");
  }
};

const uint32_t kStopTimeoutMs = 5000;

void bringUpToRunning(L::Coordinator& c) {
  ASSERT_TRUE(c.requestStart());
  ASSERT_EQ(L::State::Starting, c.state());
  ASSERT_TRUE(c.onTaskStarted());
  ASSERT_EQ(L::State::Running, c.state());
}

}  // namespace

// --- Normal ordering -------------------------------------------------------

// Handoff Phase 0: "Normal begin() -> connect -> end() ordering."
TEST(MQTTLifecycle, NormalStartRunStopCycle) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);

  EXPECT_EQ(L::State::Stopped, c.state());
  ASSERT_TRUE(c.requestStart());
  EXPECT_EQ(L::State::Starting, c.state());
  ASSERT_TRUE(c.onTaskStarted());
  EXPECT_EQ(L::State::Running, c.state());
  ASSERT_TRUE(c.requestStop());
  EXPECT_EQ(L::State::StopRequested, c.state());
  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_EQ(L::State::Stopped, c.state());

  // create_task, then deliver_stop, then a single post-ack resource release
  // and a clean OTA-barrier signal — in that order.
  const std::vector<std::string> expected = {"startTask", "deliverStop",
                                             "release", "otaClean"};
  EXPECT_EQ(expected, ops.log);
  EXPECT_EQ(1, ops.start_task_calls);
  EXPECT_EQ(1, ops.deliver_stop_calls);
  EXPECT_EQ(1, ops.release_calls);
  EXPECT_TRUE(ops.last_stop_clean);
}

// --- Idempotency: "Duplicate stop", "Idempotent start and stop requests" ---

TEST(MQTTLifecycle, StartIsIdempotent) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);

  ASSERT_TRUE(c.requestStart());
  // Second start while Starting is a no-op — no extra task creation.
  EXPECT_FALSE(c.requestStart());
  ASSERT_TRUE(c.onTaskStarted());
  // And a no-op while Running.
  EXPECT_FALSE(c.requestStart());
  EXPECT_EQ(1, ops.start_task_calls);
}

TEST(MQTTLifecycle, DuplicateStopIsIdempotent) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  ASSERT_TRUE(c.requestStop());
  // Duplicate stop requests do not re-deliver the stop.
  EXPECT_FALSE(c.requestStop());
  EXPECT_FALSE(c.requestStop());
  EXPECT_EQ(1, ops.deliver_stop_calls);

  ASSERT_TRUE(c.onTaskStopped());
  // A stop while already Stopped is also a no-op.
  EXPECT_FALSE(c.requestStop());
  EXPECT_EQ(1, ops.release_calls);
}

// --- Partial / early lifecycle --------------------------------------------

// Handoff: "stop before full initialization".
TEST(MQTTLifecycle, StopBeforeFullInitialization) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);

  ASSERT_TRUE(c.requestStart());
  EXPECT_EQ(L::State::Starting, c.state());
  // Stop arrives before StartCompleted.
  ASSERT_TRUE(c.requestStop());
  EXPECT_EQ(L::State::StopRequested, c.state());
  EXPECT_EQ(1, ops.deliver_stop_calls);
  // No resources released until the task acknowledges.
  EXPECT_EQ(0, ops.release_calls);
  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_EQ(L::State::Stopped, c.state());
  EXPECT_EQ(1, ops.release_calls);
}

// Handoff Phase 2/Phase 0: "Partial initialization failures ... releases only
// resources owned by that attempt." The rollback releases, but no OTA-barrier
// completion is signalled for a failed start.
TEST(MQTTLifecycle, StartFailureRollsBackResources) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);

  ASSERT_TRUE(c.requestStart());
  ASSERT_TRUE(c.onTaskStartFailed());
  EXPECT_EQ(L::State::Stopped, c.state());
  EXPECT_EQ(1, ops.release_calls);
  EXPECT_EQ(0, ops.stop_complete_calls);
  // Recoverable: a subsequent start is accepted.
  EXPECT_TRUE(c.mayRestart());
  EXPECT_TRUE(c.requestStart());
  EXPECT_EQ(2, ops.start_task_calls);
}

// Handoff: "restart after stop".
TEST(MQTTLifecycle, RestartAfterStop) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);

  bringUpToRunning(c);
  ASSERT_TRUE(c.requestStop());
  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_TRUE(c.mayRestart());

  ASSERT_TRUE(c.requestStart());
  EXPECT_EQ(L::State::Starting, c.state());
  ASSERT_TRUE(c.onTaskStarted());
  EXPECT_EQ(L::State::Running, c.state());
  EXPECT_EQ(2, ops.start_task_calls);
}

// --- Timeout / fallback ----------------------------------------------------

// Handoff: "Timeout/fallback behavior when the MQTT task or client does not
// acknowledge." Models the reviewed fallback replacing the abrupt vTaskDelete.
TEST(MQTTLifecycle, StopTimeoutFiresReviewedFallback) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  ops.now = 1000;
  ASSERT_TRUE(c.requestStop());
  EXPECT_EQ(L::State::StopRequested, c.state());

  // Before the deadline, tick() is inert.
  ops.now = 1000 + kStopTimeoutMs - 1;
  c.tick();
  EXPECT_EQ(L::State::StopRequested, c.state());
  EXPECT_EQ(0, ops.release_calls);

  // At the deadline the fallback fires: forced release, dirty OTA signal.
  ops.now = 1000 + kStopTimeoutMs;
  c.tick();
  EXPECT_EQ(L::State::Stopped, c.state());
  EXPECT_TRUE(c.stopTimedOut());
  EXPECT_EQ(1, ops.release_calls);
  EXPECT_EQ(1, ops.stop_complete_calls);
  EXPECT_FALSE(ops.last_stop_clean);

  // A late ack after the fallback does not double-release.
  EXPECT_FALSE(c.onTaskStopped());
  EXPECT_EQ(1, ops.release_calls);
}

TEST(MQTTLifecycle, TickWithoutPendingStopIsNoop) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  ops.now = 1'000'000;  // far past any deadline
  c.tick();
  EXPECT_EQ(L::State::Running, c.state());
  EXPECT_EQ(0, ops.release_calls);
}

TEST(MQTTLifecycle, AckBeforeTimeoutPreemptsFallback) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  ops.now = 100;
  ASSERT_TRUE(c.requestStop());
  ops.now = 100 + kStopTimeoutMs / 2;
  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_TRUE(ops.last_stop_clean);
  EXPECT_FALSE(c.stopTimedOut());

  // A later tick past the original deadline must not fire a second time.
  ops.now = 100 + kStopTimeoutMs * 10;
  c.tick();
  EXPECT_EQ(1, ops.release_calls);
  EXPECT_EQ(1, ops.stop_complete_calls);
}

// --- Ownership: no access after release ------------------------------------

// Handoff: "No client, queue, buffer, or task access after its owner releases
// it," and "A completion acknowledgment before the loop task releases queues,
// buffers, or other shared resources."
TEST(MQTTLifecycle, ResourcesReleasedOnlyAfterAcknowledgment) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  EXPECT_TRUE(c.mayTouchOwnedState());
  ASSERT_TRUE(c.requestStop());
  // Still safe to touch owned state — resources are not yet released.
  EXPECT_TRUE(c.mayTouchOwnedState());
  EXPECT_EQ(0, ops.release_calls);

  ASSERT_TRUE(c.onStopBegan());
  EXPECT_EQ(L::State::Stopping, c.state());
  EXPECT_TRUE(c.mayTouchOwnedState());
  EXPECT_EQ(0, ops.release_calls);

  ASSERT_TRUE(c.onTaskStopped());
  // Owner has released; nothing may touch owned state now.
  EXPECT_FALSE(c.mayTouchOwnedState());
  EXPECT_EQ(1, ops.release_calls);
}

// Handoff: "Cessation of new connects, publishes, retries, and
// reconfigurations" once a stop is requested.
TEST(MQTTLifecycle, NewWorkCeasesOnceStopRequested) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);

  EXPECT_FALSE(c.acceptsNewWork());  // Stopped
  ASSERT_TRUE(c.requestStart());
  EXPECT_TRUE(c.acceptsNewWork());  // Starting
  ASSERT_TRUE(c.onTaskStarted());
  EXPECT_TRUE(c.acceptsNewWork());  // Running

  ASSERT_TRUE(c.requestStop());
  EXPECT_FALSE(c.acceptsNewWork());  // StopRequested
  ASSERT_TRUE(c.onStopBegan());
  EXPECT_FALSE(c.acceptsNewWork());  // Stopping
  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_FALSE(c.acceptsNewWork());  // Stopped
}

// --- Stop while doing X (the activity matrix) ------------------------------

// Handoff: "Stop while connecting, connected, publishing, retrying, renewing a
// token, running NTP, and applying a slot change." Each activity resolves to
// the same ownership contract: the stop is delivered, new work ceases, and
// resources survive until the task acknowledges. "connecting" happens during
// Starting (before StartCompleted); the rest are Running-state activities.
TEST(MQTTLifecycle, StopDuringEveryActivityHonorsTheContract) {
  struct Activity {
    const char* label;
    bool running;  // true => Running-state activity, false => Starting
  };
  const Activity activities[] = {
      {"connecting", false},   {"connected", true},
      {"publishing", true},    {"retrying", true},
      {"renewingToken", true}, {"runningNtp", true},
      {"applyingSlotChange", true},
  };

  for (const auto& a : activities) {
    SCOPED_TRACE(a.label);
    FakeOps ops;
    L::Coordinator c(ops, kStopTimeoutMs);

    ASSERT_TRUE(c.requestStart());
    if (a.running) {
      ASSERT_TRUE(c.onTaskStarted());
      ASSERT_EQ(L::State::Running, c.state());
    } else {
      ASSERT_EQ(L::State::Starting, c.state());
    }

    ASSERT_TRUE(c.requestStop());
    EXPECT_EQ(1, ops.deliver_stop_calls);
    EXPECT_FALSE(c.acceptsNewWork());
    EXPECT_EQ(0, ops.release_calls);  // not until ack

    ASSERT_TRUE(c.onTaskStopped());
    EXPECT_EQ(L::State::Stopped, c.state());
    EXPECT_EQ(1, ops.release_calls);
    EXPECT_TRUE(ops.last_stop_clean);
  }
}

// --- Callback delivery timing ----------------------------------------------

// Handoff: "Callback delivered before stop, during stop, after disconnect, and
// after the stop acknowledgment." A callback consults mayTouchOwnedState()
// before touching owned state; only the post-acknowledgment callback is a
// no-op. ("after disconnect" is modeled as StopBegan -> Stopping, i.e. the
// task tearing the client down.)
TEST(MQTTLifecycle, CallbackGuardTracksResourceOwnership) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  EXPECT_TRUE(c.mayTouchOwnedState());  // callback before stop

  ASSERT_TRUE(c.requestStop());
  EXPECT_TRUE(c.mayTouchOwnedState());  // callback during stop (pre-teardown)

  ASSERT_TRUE(c.onStopBegan());
  EXPECT_TRUE(c.mayTouchOwnedState());  // callback after disconnect begins

  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_FALSE(c.mayTouchOwnedState());  // callback after stop-ack => no-op
}

// --- OTA teardown barrier (release-critical) -------------------------------

// Handoff OTA barrier: "firmware erase/write must not begin until MQTT shutdown
// has reached a safe acknowledgment point."
TEST(MQTTLifecycle, OtaFlashBlockedUntilCleanStopAcknowledged) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  EXPECT_FALSE(c.mayBeginFlash());  // Running
  ASSERT_TRUE(c.requestStop());
  EXPECT_FALSE(c.mayBeginFlash());  // StopRequested — not yet safe
  ASSERT_TRUE(c.onStopBegan());
  EXPECT_FALSE(c.mayBeginFlash());  // Stopping — still not safe

  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_TRUE(c.mayBeginFlash());  // clean stop => flashing permitted
  EXPECT_TRUE(ops.last_stop_clean);
}

// Handoff OTA barrier: "MQTT stop times out: OTA aborts safely rather than
// writing under uncertain ownership."
TEST(MQTTLifecycle, OtaAbortsWhenStopTimesOut) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  ops.now = 500;
  ASSERT_TRUE(c.requestStop());
  ops.now = 500 + kStopTimeoutMs;
  c.tick();

  EXPECT_EQ(L::State::Stopped, c.state());
  EXPECT_FALSE(c.mayBeginFlash());  // dirty stop => flashing withheld
  EXPECT_FALSE(ops.last_stop_clean);

  // A fresh clean start/stop cycle clears the latch and re-enables flashing.
  ASSERT_TRUE(c.requestStart());
  EXPECT_FALSE(c.stopTimedOut());
  ASSERT_TRUE(c.onTaskStarted());
  ASSERT_TRUE(c.requestStop());
  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_TRUE(c.mayBeginFlash());
}

// Handoff OTA barrier: "the bridge must not be restarted while flash writing is
// active" — expressed here as: no restart while a stop is in progress.
TEST(MQTTLifecycle, NoRestartWhileStopInProgress) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);
  bringUpToRunning(c);

  ASSERT_TRUE(c.requestStop());
  EXPECT_FALSE(c.mayRestart());
  EXPECT_FALSE(c.requestStart());  // rejected while StopRequested
  ASSERT_TRUE(c.onStopBegan());
  EXPECT_FALSE(c.mayRestart());
  EXPECT_FALSE(c.requestStart());  // rejected while Stopping

  ASSERT_TRUE(c.onTaskStopped());
  EXPECT_TRUE(c.mayRestart());
  EXPECT_TRUE(c.requestStart());
}

// Handoff OTA barrier: "Repeated failed OTA attempts do not ... leave the
// bridge permanently stopped."
TEST(MQTTLifecycle, RepeatedFailedStopsLeaveBridgeRestartable) {
  FakeOps ops;
  L::Coordinator c(ops, kStopTimeoutMs);

  for (int attempt = 0; attempt < 3; ++attempt) {
    SCOPED_TRACE(attempt);
    ASSERT_TRUE(c.requestStart());
    ASSERT_TRUE(c.onTaskStarted());
    ops.now += 1000;
    ASSERT_TRUE(c.requestStop());
    ops.now += kStopTimeoutMs;
    c.tick();  // times out (dirty)
    EXPECT_EQ(L::State::Stopped, c.state());
    EXPECT_TRUE(c.mayRestart());  // never permanently stuck
  }
  EXPECT_EQ(3, ops.start_task_calls);
}

// --- Diagnostics -----------------------------------------------------------

TEST(MQTTLifecycle, StateAndEventNamesAreStable) {
  EXPECT_STREQ("Stopped", L::stateName(L::State::Stopped));
  EXPECT_STREQ("Running", L::stateName(L::State::Running));
  EXPECT_STREQ("StopRequested", L::stateName(L::State::StopRequested));
  EXPECT_STREQ("StartRequested", L::eventName(L::Event::StartRequested));
  EXPECT_STREQ("StopTimedOut", L::eventName(L::Event::StopTimedOut));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
