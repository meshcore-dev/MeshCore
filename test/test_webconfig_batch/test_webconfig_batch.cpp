// Host contract tests for the pure WebConfig config-batch / reboot / stop state
// machine. This spec mirrors src/helpers/esp32/WebConfigServer.cpp; it is not
// yet wired into the bridge (see WebConfigBatch.h scope note).
#include <gtest/gtest.h>

#include <stdint.h>
#include <limits>

#include "helpers/WebConfigBatch.h"

namespace Batch = WebConfigBatch;
using State = WebConfigBatch::State;

// --------------------------------------------------------------------------
// POST accept classification
// --------------------------------------------------------------------------
TEST(WebConfigBatch, IdleAcceptsANewBatchWithChangesOrRebootOnly) {
  // A normal save (changes present) is accepted.
  EXPECT_EQ(Batch::PostOutcome::Accept,
            Batch::classifyPost(State::Idle, /*reqid_matches=*/false, /*count=*/3, false));
  // A reboot-only request (no changes) is still accepted.
  EXPECT_EQ(Batch::PostOutcome::Accept,
            Batch::classifyPost(State::Idle, false, 0, /*reboot_after=*/true));
}

TEST(WebConfigBatch, IdleWithNothingToDoIsNoChanges) {
  EXPECT_EQ(Batch::PostOutcome::NoChanges,
            Batch::classifyPost(State::Idle, false, 0, false));
}

TEST(WebConfigBatch, SameReqidReplaysWithoutReapplyingWhilePendingOrDone) {
  // The idempotent-replay path fires for BOTH in-flight and finished batches
  // when the reqid matches; commands are never re-applied.
  EXPECT_EQ(Batch::PostOutcome::Replay,
            Batch::classifyPost(State::Pending, /*reqid_matches=*/true, 3, false));
  EXPECT_EQ(Batch::PostOutcome::Replay,
            Batch::classifyPost(State::Done, /*reqid_matches=*/true, 3, false));
  // Replay wins even over a would-be no-changes request.
  EXPECT_EQ(Batch::PostOutcome::Replay,
            Batch::classifyPost(State::Pending, true, 0, false));
  // The replay body reports the batch's own state.
  EXPECT_STREQ("pending", Batch::replayStateName(State::Pending));
  EXPECT_STREQ("done", Batch::replayStateName(State::Done));
}

TEST(WebConfigBatch, DifferentReqidIsBusyOnlyWhilePending) {
  // A second client (different reqid) while a batch is still draining => busy.
  EXPECT_EQ(Batch::PostOutcome::Busy,
            Batch::classifyPost(State::Pending, /*reqid_matches=*/false, 2, false));
  // But once the previous batch is DONE, a different reqid is NOT busy: the new
  // batch overwrites the finished slot (asymmetry with the Pending case).
  EXPECT_EQ(Batch::PostOutcome::Accept,
            Batch::classifyPost(State::Done, false, 2, false));
  EXPECT_EQ(Batch::PostOutcome::NoChanges,
            Batch::classifyPost(State::Done, false, 0, false));
}

// --------------------------------------------------------------------------
// Drain pacing / all_ok / finish
// --------------------------------------------------------------------------
TEST(WebConfigBatch, DrainNeverWaitsBeforeTheFirstOrAfterTheLastCommand) {
  // batch_next == 0: the first command runs immediately (fires onConfigBatchStart).
  EXPECT_FALSE(Batch::drainMustWait(0, 5, 1000, 1000));
  // batch_next >= batch_count: nothing left to pace.
  EXPECT_FALSE(Batch::drainMustWait(5, 5, 1000, 1000));
  // A single-command (or reboot-only) batch never paces.
  EXPECT_FALSE(Batch::drainMustWait(0, 1, 1000, 1000));
  EXPECT_FALSE(Batch::drainMustWait(0, 0, 1000, 1000));
}

TEST(WebConfigBatch, DrainPacesTwentyFiveMillisBetweenCommandsWithInclusiveRelease) {
  const uint32_t last = 1000;
  // 24 ms after the previous command: still waiting.
  EXPECT_TRUE(Batch::drainMustWait(2, 5, last + 24, last));
  // Exactly 25 ms: the gate releases (production uses `< 25`).
  EXPECT_FALSE(Batch::drainMustWait(2, 5, last + 25, last));
  EXPECT_FALSE(Batch::drainMustWait(2, 5, last + 26, last));
}

TEST(WebConfigBatch, DrainPacingSurvivesMillisRollover) {
  const uint32_t last = std::numeric_limits<uint32_t>::max() - 10;
  EXPECT_TRUE(Batch::drainMustWait(2, 5, last + 24, last));   // 24 ms elapsed, wrapped
  EXPECT_FALSE(Batch::drainMustWait(2, 5, last + 25, last));  // 25 ms elapsed, wrapped
}

TEST(WebConfigBatch, AllOkIsAStickyAndAcrossCommandReplies) {
  bool all_ok = true;
  all_ok = Batch::nextAllOk(all_ok, true);
  EXPECT_TRUE(all_ok);
  all_ok = Batch::nextAllOk(all_ok, false);  // one command failed
  EXPECT_FALSE(all_ok);
  all_ok = Batch::nextAllOk(all_ok, true);   // stays false forever after
  EXPECT_FALSE(all_ok);
}

TEST(WebConfigBatch, DrainFinishesWhenTheIndexReachesTheCount) {
  EXPECT_FALSE(Batch::drainFinished(4, 5));
  EXPECT_TRUE(Batch::drainFinished(5, 5));
  EXPECT_TRUE(Batch::drainFinished(0, 0));  // reboot-only / empty batch
}

TEST(WebConfigBatch, FinishArmsThirtySecondFallbackOnlyForAFullyOkRebootBatch) {
  const uint32_t now = 100000;
  EXPECT_EQ(now + Batch::kRebootFallbackMs,
            Batch::finishRebootAt(/*reboot=*/true, /*all_ok=*/true, now));
  // A partially-failed batch never reboots.
  EXPECT_EQ(0u, Batch::finishRebootAt(true, false, now));
  // No reboot requested.
  EXPECT_EQ(0u, Batch::finishRebootAt(false, true, now));
}

// --------------------------------------------------------------------------
// Result read
// --------------------------------------------------------------------------
TEST(WebConfigBatch, ResultReadClassifiesIdlePendingDoneAndUnknownReqid) {
  // Idle: any valid reqid gets "idle" (no reqid check while idle).
  EXPECT_EQ(Batch::ResultOutcome::Idle, Batch::classifyResult(State::Idle, false));
  EXPECT_EQ(Batch::ResultOutcome::Idle, Batch::classifyResult(State::Idle, true));
  // Matching reqid reflects the batch state.
  EXPECT_EQ(Batch::ResultOutcome::Pending, Batch::classifyResult(State::Pending, true));
  EXPECT_EQ(Batch::ResultOutcome::Done, Batch::classifyResult(State::Done, true));
  // A live/finished batch with a mismatched reqid is unknown (404).
  EXPECT_EQ(Batch::ResultOutcome::Unknown, Batch::classifyResult(State::Pending, false));
  EXPECT_EQ(Batch::ResultOutcome::Unknown, Batch::classifyResult(State::Done, false));
}

TEST(WebConfigBatch, DoneBodyAdvertisesRebootOnlyWhenFullyOk) {
  EXPECT_TRUE(Batch::doneReportsReboot(true, true));
  EXPECT_FALSE(Batch::doneReportsReboot(true, false));
  EXPECT_FALSE(Batch::doneReportsReboot(false, true));
}

TEST(WebConfigBatch, FirstDoneReadArmsTheThreeSecondRebootExactlyOnce) {
  const uint32_t now = 500000;
  // First read of a fully-OK reboot batch arms.
  EXPECT_TRUE(Batch::shouldArmConfirmReboot(State::Done, true, true, /*already_armed=*/false));
  EXPECT_EQ(now + Batch::kRebootConfirmMs, Batch::confirmRebootAt(now));
  // Already armed => never re-arms (polling can't push the deadline out).
  EXPECT_FALSE(Batch::shouldArmConfirmReboot(State::Done, true, true, /*already_armed=*/true));
  // Not applicable while pending, without reboot, or after a partial failure.
  EXPECT_FALSE(Batch::shouldArmConfirmReboot(State::Pending, true, true, false));
  EXPECT_FALSE(Batch::shouldArmConfirmReboot(State::Done, false, true, false));
  EXPECT_FALSE(Batch::shouldArmConfirmReboot(State::Done, true, false, false));
}

// --------------------------------------------------------------------------
// Reboot fire / pending
// --------------------------------------------------------------------------
TEST(WebConfigBatch, RebootFiresAtOrAfterTheDeadlineAndNeverWhenUnscheduled) {
  EXPECT_FALSE(Batch::rebootDue(0, 1234567));           // 0 == unscheduled
  const uint32_t at = 100000;
  EXPECT_FALSE(Batch::rebootDue(at, at - 1));
  EXPECT_TRUE(Batch::rebootDue(at, at));                // inclusive boundary
  EXPECT_TRUE(Batch::rebootDue(at, at + 1));
}

TEST(WebConfigBatch, RebootDueSurvivesMillisRollover) {
  const uint32_t at = std::numeric_limits<uint32_t>::max() - 5;  // near the top
  EXPECT_FALSE(Batch::rebootDue(at, at - 1));
  EXPECT_TRUE(Batch::rebootDue(at, at));
  EXPECT_TRUE(Batch::rebootDue(at, at + 10));  // now has wrapped past zero
}

TEST(WebConfigBatch, OnlyAConfigSaveRebootInDoneStateReportsPending) {
  EXPECT_TRUE(Batch::isConfigRebootPending(/*reboot_at=*/123, /*batch_reboot=*/true, State::Done));
  // Manual /api/reboot: reboot_at set but batch_reboot false => not "pending".
  EXPECT_FALSE(Batch::isConfigRebootPending(123, false, State::Done));
  // Not yet done, or nothing scheduled.
  EXPECT_FALSE(Batch::isConfigRebootPending(123, true, State::Pending));
  EXPECT_FALSE(Batch::isConfigRebootPending(0, true, State::Done));
}

// --------------------------------------------------------------------------
// Stop gating
// --------------------------------------------------------------------------
TEST(WebConfigBatch, StopFinalizesOnlyWhenNoHandlersAreInFlight) {
  EXPECT_EQ(Batch::StopAction::Finalize,
            Batch::stopStep(/*refs=*/0, /*warned=*/false, /*warn_at=*/50000, /*now=*/60000));
}

TEST(WebConfigBatch, StopWarnsOnceAfterTheDeadlineThenKeepsWaiting) {
  const uint32_t warn_at = 50000;
  // Before the warn deadline with handlers in flight: just wait.
  EXPECT_EQ(Batch::StopAction::Wait, Batch::stopStep(2, false, warn_at, warn_at - 1));
  // At the deadline, not yet warned: warn once.
  EXPECT_EQ(Batch::StopAction::Warn, Batch::stopStep(2, false, warn_at, warn_at));
  // Already warned: keep waiting (never warns again, never forces teardown).
  EXPECT_EQ(Batch::StopAction::Wait, Batch::stopStep(2, true, warn_at, warn_at + 100000));
  // An unscheduled warn timer never warns.
  EXPECT_EQ(Batch::StopAction::Wait, Batch::stopStep(2, false, 0, 999999));
}

// --------------------------------------------------------------------------
// CLI sequences (/api/cli), which share the deferred-command slot
// --------------------------------------------------------------------------
TEST(WebConfigBatch, CliReadPagesResultsAndNeverOverrunsWhatHasDrained) {
  const int page = Batch::kCliResultPage;
  // Nothing drained past the cursor yet.
  EXPECT_EQ(0, Batch::cliPageCount(/*from=*/0, /*produced=*/0, page));
  EXPECT_EQ(0, Batch::cliPageCount(/*from=*/3, /*produced=*/3, page));
  // Partial progress: hand back exactly what exists.
  EXPECT_EQ(3, Batch::cliPageCount(0, 3, page));
  EXPECT_EQ(2, Batch::cliPageCount(5, 7, page));
  // More available than fits in one read: cap at the page size.
  EXPECT_EQ(page, Batch::cliPageCount(0, page + 5, page));
  // A cursor beyond what has drained (stale or crafted) yields nothing rather
  // than a negative count that would index backwards through the batch.
  EXPECT_EQ(0, Batch::cliPageCount(/*from=*/9, /*produced=*/4, page));
}

TEST(WebConfigBatch, CliReadIsDoneOnlyOnceEveryResultHasBeenHandedOver) {
  // Still executing: never final, however much has been read.
  EXPECT_FALSE(Batch::cliReadIsFinal(State::Pending, /*from=*/0, /*page=*/8, /*total=*/8));
  // Execution finished but the client has only seen the first page. Reporting
  // "done" here would make a client that stops polling lose the rest.
  EXPECT_FALSE(Batch::cliReadIsFinal(State::Done, /*from=*/0, /*page=*/8, /*total=*/20));
  EXPECT_FALSE(Batch::cliReadIsFinal(State::Done, /*from=*/8, /*page=*/8, /*total=*/20));
  // The read that hands over the last result is the final one.
  EXPECT_TRUE(Batch::cliReadIsFinal(State::Done, /*from=*/16, /*page=*/4, /*total=*/20));
  // Re-reading past the end stays final (polls after the last page).
  EXPECT_TRUE(Batch::cliReadIsFinal(State::Done, /*from=*/20, /*page=*/0, /*total=*/20));
}

// Every string below is a literal lifted from CommonCLI.cpp /
// CommonCLI_Observer.cpp. Testing only for an "Err" prefix passed five of these
// off as success, which both coloured them green and let a queued reboot go
// ahead after them.
TEST(WebConfigBatch, CliFailureRepliesAreRecognisedInEveryShapeCommonCLIEmits) {
  const char* failures[] = {
    "Err - bad params",                          // MyMesh setperm
    "ERR: bad pubkey",                           // neighbor.remove
    "Error: IATA code must be exactly 3 letters",// observer setters
    "(ERR: clock cannot go backwards)",          // clock sync, parenthesised
    "Unknown command",                           // top-level fallthrough
    "unknown config: mqtt.nope",                 // set fallthrough
    "??: mqtt.nope",                             // get fallthrough
    "Can't find GPS",                            // gps
    "File system erase: Err",                    // failure reported at the end
  };
  for (const char* f : failures) {
    EXPECT_TRUE(Batch::cliReplyIsFailure(f)) << f;
  }

  const char* successes[] = {
    "OK",
    "OK - slot 1 preset: meshrank",
    "> 22",                                      // getter value
    "> msgs: on, 1: analyzer-us (ok)",           // getter, contains "ok"
    "File system erase: OK",                     // same shape, succeeded
    "Free: 142832, Min: 126808",                 // memory
    "v1.16.0 (Build: 6 Jun 2026)",               // ver
  };
  for (const char* s : successes) {
    EXPECT_FALSE(Batch::cliReplyIsFailure(s)) << s;
  }
  // An empty reply is normalised to "OK" before it ever reaches the client.
  EXPECT_FALSE(Batch::cliReplyIsFailure(""));
  EXPECT_FALSE(Batch::cliReplyIsFailure(NULL));
}

TEST(WebConfigBatch, OnlyWritesGateTheDeferredReboot) {
  // Writes gate it: these are what can leave a config not worth rebooting into.
  EXPECT_TRUE(Batch::cliReplyGatesReboot("set tx 22"));
  EXPECT_TRUE(Batch::cliReplyGatesReboot("set mqtt1.preset meshrank"));
  EXPECT_TRUE(Batch::cliReplyGatesReboot("password hunter2"));
  // Diagnostics do not. `memory` answering "Free: ..." must not be read as a
  // failure and strand the operator's reboot, and a getter's "> value" must not
  // be read as a success either — neither is asked.
  EXPECT_FALSE(Batch::cliReplyGatesReboot("memory"));
  EXPECT_FALSE(Batch::cliReplyGatesReboot("get tx"));
  EXPECT_FALSE(Batch::cliReplyGatesReboot("reboot"));
  EXPECT_FALSE(Batch::cliReplyGatesReboot("advert"));
  EXPECT_FALSE(Batch::cliReplyGatesReboot(NULL));
  // "settle" must not be mistaken for a `set`; the space is part of the token.
  EXPECT_FALSE(Batch::cliReplyGatesReboot("settle"));

  // A write counts only on the "OK" prefix every setter keeps.
  EXPECT_TRUE(Batch::cliWriteSucceeded("OK"));
  EXPECT_TRUE(Batch::cliWriteSucceeded("OK - reboot to apply"));
  EXPECT_FALSE(Batch::cliWriteSucceeded("unknown config: nope"));
  EXPECT_FALSE(Batch::cliWriteSucceeded("Error: expected a number"));
  EXPECT_FALSE(Batch::cliWriteSucceeded(""));
}

TEST(WebConfigBatch, CliRebootIsWithheldWhenAnyCommandInTheSequenceFailed) {
  EXPECT_TRUE(Batch::cliRebootAllowed(/*has_reboot=*/true, /*all_ok=*/true));
  // Same rule a config save follows: do not reboot into a half-applied config
  // over a link the operator may not get back.
  EXPECT_FALSE(Batch::cliRebootAllowed(true, false));
  // No `reboot` in the sequence: nothing to allow either way.
  EXPECT_FALSE(Batch::cliRebootAllowed(false, true));
  EXPECT_FALSE(Batch::cliRebootAllowed(false, false));
}

// --------------------------------------------------------------------------
// Wrap-around guard shared with the production _reboot_at assignments
// --------------------------------------------------------------------------
TEST(WebConfigBatch, ScheduleAtNeverReturnsTheUnscheduledSentinel) {
  EXPECT_EQ(1000u + 3000u, Batch::scheduleAt(1000, 3000));
  // A deadline that lands exactly on 0 is bumped to 1 so it still means "set".
  const uint32_t just_below_wrap = std::numeric_limits<uint32_t>::max();  // +1 wraps to 0
  EXPECT_EQ(1u, Batch::scheduleAt(just_below_wrap, 1));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
