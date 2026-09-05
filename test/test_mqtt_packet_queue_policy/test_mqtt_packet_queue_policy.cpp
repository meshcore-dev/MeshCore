#include <gtest/gtest.h>

#include <stdint.h>

#include "helpers/MQTTPacketQueuePolicy.h"

namespace QueuePolicy = MQTTPacketQueuePolicy;

TEST(MQTTPacketQueuePolicy, EnqueuesWhileCapacityRemains) {
  EXPECT_EQ(QueuePolicy::EnqueueAction::Enqueue,
            QueuePolicy::enqueueAction(0, 6));
  EXPECT_EQ(QueuePolicy::EnqueueAction::Enqueue,
            QueuePolicy::enqueueAction(5, 6));
}

TEST(MQTTPacketQueuePolicy, EvictsOldestAtOrAboveCapacity) {
  EXPECT_EQ(QueuePolicy::EnqueueAction::EvictOldestThenEnqueue,
            QueuePolicy::enqueueAction(6, 6));
  EXPECT_EQ(QueuePolicy::EnqueueAction::EvictOldestThenEnqueue,
            QueuePolicy::enqueueAction(7, 6));
}

TEST(MQTTPacketQueuePolicy, RejectsQueueWithZeroCapacity) {
  EXPECT_EQ(QueuePolicy::EnqueueAction::Reject,
            QueuePolicy::enqueueAction(0, 0));
}

TEST(MQTTPacketQueuePolicy, DisconnectedQueueFlushesAtExactStaleBoundary) {
  const uint32_t started = 1000;
  EXPECT_FALSE(QueuePolicy::shouldFlushDisconnected(
      started + QueuePolicy::kDisconnectedStaleMs - 1, started));
  EXPECT_TRUE(QueuePolicy::shouldFlushDisconnected(
      started + QueuePolicy::kDisconnectedStaleMs, started));
  EXPECT_TRUE(QueuePolicy::shouldFlushDisconnected(
      started + QueuePolicy::kDisconnectedStaleMs + 1, started));
}

TEST(MQTTPacketQueuePolicy, ZeroDisconnectedTimestampMeansNotStarted) {
  EXPECT_FALSE(QueuePolicy::shouldFlushDisconnected(UINT32_MAX, 0));
}

TEST(MQTTPacketQueuePolicy, DisconnectedStaleTimerSurvivesMillisWrap) {
  const uint32_t started = UINT32_MAX - 100;
  EXPECT_FALSE(QueuePolicy::shouldFlushDisconnected(198, started, 300));
  EXPECT_TRUE(QueuePolicy::shouldFlushDisconnected(199, started, 300));
}

TEST(MQTTPacketQueuePolicy, DrainIsGentleThroughFivePackets) {
  for (size_t count = 0; count <= QueuePolicy::kBacklogThreshold; ++count) {
    const QueuePolicy::DrainBudget budget = QueuePolicy::drainBudget(count);
    EXPECT_EQ(QueuePolicy::kGentleDrainCount, budget.max_packets) << count;
    EXPECT_EQ(QueuePolicy::kGentleDrainBudgetMs, budget.max_time_ms) << count;
  }
}

TEST(MQTTPacketQueuePolicy, DrainBurstsAboveFivePackets) {
  const QueuePolicy::DrainBudget budget =
      QueuePolicy::drainBudget(QueuePolicy::kBacklogThreshold + 1);
  EXPECT_EQ(QueuePolicy::kBurstDrainCount, budget.max_packets);
  EXPECT_EQ(QueuePolicy::kBurstDrainBudgetMs, budget.max_time_ms);
}

TEST(MQTTPacketQueuePolicy, DrainTimeBudgetUsesInclusiveBoundary) {
  EXPECT_TRUE(QueuePolicy::drainTimeAvailable(129, 100, 30));
  EXPECT_TRUE(QueuePolicy::drainTimeAvailable(130, 100, 30));
  EXPECT_FALSE(QueuePolicy::drainTimeAvailable(131, 100, 30));
}

TEST(MQTTPacketQueuePolicy, DrainTimeBudgetSurvivesMillisWrap) {
  const uint32_t started = UINT32_MAX - 10;
  EXPECT_TRUE(QueuePolicy::drainTimeAvailable(19, started, 30));
  EXPECT_FALSE(QueuePolicy::drainTimeAvailable(20, started, 30));
}

TEST(MQTTPacketQueuePolicy, NewPacketIsReadyWithoutRetryDeadline) {
  EXPECT_TRUE(QueuePolicy::retryReady(100, 0, 0));
  EXPECT_TRUE(QueuePolicy::retryReady(100, 500, 0));
}

TEST(MQTTPacketQueuePolicy, RetryBecomesReadyAtExactDeadline) {
  EXPECT_FALSE(QueuePolicy::retryReady(499, 500, 1));
  EXPECT_TRUE(QueuePolicy::retryReady(500, 500, 1));
  EXPECT_TRUE(QueuePolicy::retryReady(501, 500, 1));
}

TEST(MQTTPacketQueuePolicy, RetryDeadlineSurvivesMillisWrap) {
  const uint32_t deadline = 100;
  EXPECT_FALSE(QueuePolicy::retryReady(UINT32_MAX - 50, deadline, 1));
  EXPECT_FALSE(QueuePolicy::retryReady(99, deadline, 1));
  EXPECT_TRUE(QueuePolicy::retryReady(100, deadline, 1));
}

TEST(MQTTPacketQueuePolicy, WrappedZeroIsARealRetryDeadline) {
  EXPECT_FALSE(QueuePolicy::retryReady(UINT32_MAX, 0, 1));
  EXPECT_TRUE(QueuePolicy::retryReady(0, 0, 1));
}

TEST(MQTTPacketQueuePolicy, SuccessfulPublishCompletesWithoutChangingAttempts) {
  const QueuePolicy::RetryDecision decision =
      QueuePolicy::retryDecision(true, 2, 1234);
  EXPECT_EQ(QueuePolicy::RetryAction::Complete, decision.action);
  EXPECT_EQ(2, decision.retry_attempts);
  EXPECT_EQ(0U, decision.delay_ms);
  EXPECT_EQ(0U, decision.next_retry_ms);
}

TEST(MQTTPacketQueuePolicy, FailedPublishSchedulesBoundedRetry) {
  const QueuePolicy::RetryDecision minimum =
      QueuePolicy::retryDecision(false, 0, 400);
  EXPECT_EQ(QueuePolicy::RetryAction::Schedule, minimum.action);
  EXPECT_EQ(1, minimum.retry_attempts);
  EXPECT_EQ(300U, minimum.delay_ms);
  EXPECT_EQ(700U, minimum.next_retry_ms);

  const QueuePolicy::RetryDecision maximum =
      QueuePolicy::retryDecision(false, 1, 599);
  EXPECT_EQ(QueuePolicy::RetryAction::Schedule, maximum.action);
  EXPECT_EQ(2, maximum.retry_attempts);
  EXPECT_EQ(499U, maximum.delay_ms);
  EXPECT_EQ(1098U, maximum.next_retry_ms);
}

TEST(MQTTPacketQueuePolicy, ThirdFailedPublishSchedulesFinalRetry) {
  const QueuePolicy::RetryDecision decision =
      QueuePolicy::retryDecision(false, 2, 1000);
  EXPECT_EQ(QueuePolicy::RetryAction::Schedule, decision.action);
  EXPECT_EQ(QueuePolicy::kMaxQos0RetryAttempts, decision.retry_attempts);
}

TEST(MQTTPacketQueuePolicy, FailureAfterFinalRetryDropsPacket) {
  const QueuePolicy::RetryDecision decision =
      QueuePolicy::retryDecision(false, QueuePolicy::kMaxQos0RetryAttempts, 1000);
  EXPECT_EQ(QueuePolicy::RetryAction::Drop, decision.action);
  EXPECT_EQ(QueuePolicy::kMaxQos0RetryAttempts, decision.retry_attempts);
  EXPECT_EQ(0U, decision.delay_ms);
  EXPECT_EQ(0U, decision.next_retry_ms);
}

TEST(MQTTPacketQueuePolicy, RetrySchedulingDeadlineMayWrapToZero) {
  // This timestamp has jitter 98, so its 398 ms delay wraps to exactly zero.
  const uint32_t now = UINT32_MAX - 397;
  ASSERT_EQ(98U, now % QueuePolicy::kRetryDelayJitterMs);
  const QueuePolicy::RetryDecision decision =
      QueuePolicy::retryDecision(false, 0, now);
  EXPECT_EQ(QueuePolicy::RetryAction::Schedule, decision.action);
  EXPECT_EQ(398U, decision.delay_ms);
  EXPECT_EQ(0U, decision.next_retry_ms);
  EXPECT_FALSE(QueuePolicy::retryReady(UINT32_MAX, decision.next_retry_ms,
                                       decision.retry_attempts));
  EXPECT_TRUE(QueuePolicy::retryReady(0, decision.next_retry_ms,
                                      decision.retry_attempts));
}

TEST(MQTTPacketQueuePolicy, PartialPublishCountsAsDeliveredEitherWay) {
  EXPECT_TRUE(QueuePolicy::queuedPacketPublished(true, true));
  EXPECT_TRUE(QueuePolicy::queuedPacketPublished(true, false));   // packet ok, raw failed
  EXPECT_TRUE(QueuePolicy::queuedPacketPublished(false, true));   // raw ok, packet failed
  EXPECT_FALSE(QueuePolicy::queuedPacketPublished(false, false)); // neither reached a slot
}

TEST(MQTTPacketQueuePolicy, PublishOutcomePairingDrivesRetryDecision) {
  // packet succeeds / raw fails -> completed, no retry.
  QueuePolicy::RetryDecision d =
      QueuePolicy::retryDecision(QueuePolicy::queuedPacketPublished(true, false), 0, 1234U);
  EXPECT_EQ(QueuePolicy::RetryAction::Complete, d.action);

  // raw succeeds / packet fails -> also completed.
  d = QueuePolicy::retryDecision(QueuePolicy::queuedPacketPublished(false, true), 0, 1234U);
  EXPECT_EQ(QueuePolicy::RetryAction::Complete, d.action);

  // both fail on a fresh packet -> scheduled for a bounded retry.
  d = QueuePolicy::retryDecision(QueuePolicy::queuedPacketPublished(false, false), 0, 1234U);
  EXPECT_EQ(QueuePolicy::RetryAction::Schedule, d.action);
  EXPECT_EQ(1U, d.retry_attempts);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
