#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure queue/backpressure policy shared by the FreeRTOS and circular-buffer
// MQTT packet queues. Keeping the timing and retry decisions here makes the
// production behavior deterministic under host tests without mocking either
// queue implementation or the MQTT client.
namespace MQTTPacketQueuePolicy {

static const uint32_t kDisconnectedStaleMs = 300000UL;
static const size_t kBacklogThreshold = 5;
static const uint8_t kGentleDrainCount = 1;
static const uint8_t kBurstDrainCount = 5;
static const uint32_t kGentleDrainBudgetMs = 30UL;
static const uint32_t kBurstDrainBudgetMs = 100UL;
static const uint8_t kMaxQos0RetryAttempts = 3;
static const uint32_t kRetryDelayBaseMs = 300UL;
static const uint32_t kRetryDelayJitterMs = 200UL;

// Unsigned subtraction is the standard millis() idiom and remains correct
// across one 32-bit counter rollover.
static inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return now - then;
}

enum class EnqueueAction : uint8_t {
  Enqueue,
  EvictOldestThenEnqueue,
  Reject
};

static inline EnqueueAction enqueueAction(size_t queue_count, size_t capacity) {
  if (capacity == 0) return EnqueueAction::Reject;
  return queue_count >= capacity
             ? EnqueueAction::EvictOldestThenEnqueue
             : EnqueueAction::Enqueue;
}

// disconnected_since == 0 means tracking has not started. The bridge records
// the first disconnected observation and asks this helper on later cycles.
static inline bool shouldFlushDisconnected(uint32_t now,
                                           uint32_t disconnected_since,
                                           uint32_t stale_ms = kDisconnectedStaleMs) {
  return disconnected_since != 0 && elapsedMs(now, disconnected_since) >= stale_ms;
}

struct DrainBudget {
  uint8_t max_packets;
  uint32_t max_time_ms;
};

static inline DrainBudget drainBudget(size_t queue_count) {
  if (queue_count > kBacklogThreshold) {
    return {kBurstDrainCount, kBurstDrainBudgetMs};
  }
  return {kGentleDrainCount, kGentleDrainBudgetMs};
}

static inline bool drainTimeAvailable(uint32_t now, uint32_t started_at,
                                      uint32_t budget_ms) {
  // Preserve the bridge's inclusive boundary: work may begin at exactly the
  // configured limit, but not one millisecond later.
  return elapsedMs(now, started_at) <= budget_ms;
}

// retry_attempts distinguishes an unscheduled packet from a scheduled retry
// whose deadline wrapped to exactly zero. Deadlines are always less than
// 500 ms away, so the half-range comparison is unambiguous.
static inline bool retryReady(uint32_t now, uint32_t next_retry_ms,
                              uint8_t retry_attempts) {
  if (retry_attempts == 0) return true;
  return elapsedMs(now, next_retry_ms) < 0x80000000UL;
}

enum class RetryAction : uint8_t {
  Complete,
  Schedule,
  Drop
};

struct RetryDecision {
  RetryAction action;
  uint8_t retry_attempts;
  uint32_t delay_ms;
  uint32_t next_retry_ms;
};

// A queued packet counts as delivered if EITHER its structured-packet publish
// or its raw-frame publish reached at least one slot. Partial success (one
// succeeds while the other fails or was not attempted) is still success — the
// packet completes and is not retried. This is the (packet, raw) outcome pairing
// fed to retryDecision(); naming it keeps the "partial publish = done" contract
// explicit and host-tested rather than inline in the bridge's queue drain.
static inline bool queuedPacketPublished(bool packet_published,
                                         bool raw_published) {
  return packet_published || raw_published;
}

static inline RetryDecision retryDecision(bool any_published,
                                          uint8_t retry_attempts,
                                          uint32_t now) {
  if (any_published) {
    return {RetryAction::Complete, retry_attempts, 0, 0};
  }
  if (retry_attempts >= kMaxQos0RetryAttempts) {
    return {RetryAction::Drop, retry_attempts, 0, 0};
  }

  const uint32_t delay = kRetryDelayBaseMs + (now % kRetryDelayJitterMs);
  return {RetryAction::Schedule, static_cast<uint8_t>(retry_attempts + 1),
          delay, now + delay};
}

} // namespace MQTTPacketQueuePolicy
