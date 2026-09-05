#include <gtest/gtest.h>
#include <stdint.h>
#include <limits>

#include "helpers/MQTTConnectionPolicy.h"

namespace Policy = MQTTConnectionPolicy;

TEST(MQTTConnectionPolicy, ElapsedTimeHandlesNormalAndWrappedClocks) {
  EXPECT_EQ(4000U, Policy::elapsedMs(5000U, 1000U));

  const uint32_t before_wrap = std::numeric_limits<uint32_t>::max() - 99U;
  EXPECT_EQ(150U, Policy::elapsedMs(50U, before_wrap));
}

TEST(MQTTConnectionPolicy, CrossSlotReconnectGuardHasExactBoundary) {
  EXPECT_TRUE(Policy::reconnectGuardActive(14999U, 0U));
  EXPECT_FALSE(Policy::reconnectGuardActive(15000U, 0U));

  const uint32_t last = std::numeric_limits<uint32_t>::max() - 9999U;
  EXPECT_TRUE(Policy::reconnectGuardActive(4999U, last));
  EXPECT_FALSE(Policy::reconnectGuardActive(5000U, last));
}

TEST(MQTTConnectionPolicy, StableResetRequiresARealStartAndFullWindow) {
  EXPECT_FALSE(Policy::stableConnection(500000U, 0U));
  EXPECT_FALSE(Policy::stableConnection(120999U, 1000U));
  EXPECT_TRUE(Policy::stableConnection(121000U, 1000U));
}

TEST(MQTTConnectionPolicy, StableResetWindowSurvivesMillisRollover) {
  const uint32_t connected_at = std::numeric_limits<uint32_t>::max() - 59999U;
  EXPECT_FALSE(Policy::stableConnection(59999U, connected_at));
  EXPECT_TRUE(Policy::stableConnection(60000U, connected_at));
}

TEST(MQTTConnectionPolicy, BackoffLadderSaturatesAtFiveMinutes) {
  EXPECT_EQ(10000U, Policy::reconnectBackoffMs(0));
  EXPECT_EQ(30000U, Policy::reconnectBackoffMs(1));
  EXPECT_EQ(60000U, Policy::reconnectBackoffMs(2));
  EXPECT_EQ(120000U, Policy::reconnectBackoffMs(3));
  EXPECT_EQ(300000U, Policy::reconnectBackoffMs(4));
  EXPECT_EQ(300000U, Policy::reconnectBackoffMs(5));
  EXPECT_EQ(300000U, Policy::reconnectBackoffMs(255));
}

TEST(MQTTConnectionPolicy, LaterSlotsReceiveThreeSecondStagger) {
  EXPECT_EQ(10000U, Policy::reconnectDelayMs(0, 0));
  EXPECT_EQ(13000U, Policy::reconnectDelayMs(0, 1));
  EXPECT_EQ(315000U, Policy::reconnectDelayMs(5, 5));
}

TEST(MQTTConnectionPolicy, ReconnectDueUsesDelayBoundaryAndWrapSafeElapsedTime) {
  EXPECT_FALSE(Policy::reconnectDue(12999U, 0U, 0, 1));
  EXPECT_TRUE(Policy::reconnectDue(13000U, 0U, 0, 1));

  const uint32_t last = std::numeric_limits<uint32_t>::max() - 4999U;
  EXPECT_FALSE(Policy::reconnectDue(4999U, last, 0, 0));
  EXPECT_TRUE(Policy::reconnectDue(5000U, last, 0, 0));
}

TEST(MQTTConnectionPolicy, BackoffAdvanceClimbsThenCountsFailuresAtMaximum) {
  Policy::BackoffAdvance first = Policy::advanceBackoff(0, 0);
  EXPECT_EQ(1, first.reconnect_backoff);
  EXPECT_EQ(0, first.max_backoff_failures);
  EXPECT_FALSE(first.circuit_breaker_tripped);
  EXPECT_TRUE(first.should_reconnect);

  Policy::BackoffAdvance enters_maximum = Policy::advanceBackoff(4, 0);
  EXPECT_EQ(5, enters_maximum.reconnect_backoff);
  EXPECT_EQ(0, enters_maximum.max_backoff_failures);
  EXPECT_FALSE(enters_maximum.circuit_breaker_tripped);
  EXPECT_TRUE(enters_maximum.should_reconnect);

  Policy::BackoffAdvance first_max_failure = Policy::advanceBackoff(5, 0);
  EXPECT_EQ(5, first_max_failure.reconnect_backoff);
  EXPECT_EQ(1, first_max_failure.max_backoff_failures);
  EXPECT_FALSE(first_max_failure.circuit_breaker_tripped);
  EXPECT_TRUE(first_max_failure.should_reconnect);
}

TEST(MQTTConnectionPolicy, ThirdFailureAtMaximumTripsWithoutAnotherHandshake) {
  Policy::BackoffAdvance result = Policy::advanceBackoff(5, 2);
  EXPECT_EQ(5, result.reconnect_backoff);
  EXPECT_EQ(3, result.max_backoff_failures);
  EXPECT_TRUE(result.circuit_breaker_tripped);
  EXPECT_FALSE(result.should_reconnect);
}

TEST(MQTTConnectionPolicy, CircuitBreakerProbeHasExactThirtyMinuteBoundary) {
  EXPECT_FALSE(Policy::circuitBreakerProbeDue(1799999U, 0U));
  EXPECT_TRUE(Policy::circuitBreakerProbeDue(1800000U, 0U));

  const uint32_t last = std::numeric_limits<uint32_t>::max() - 899999U;
  EXPECT_FALSE(Policy::circuitBreakerProbeDue(899999U, last));
  EXPECT_TRUE(Policy::circuitBreakerProbeDue(900000U, last));
}

TEST(MQTTConnectionPolicy, JwtLifetimeUsesCappedPerSlotStagger) {
  EXPECT_EQ(86400U, Policy::jwtLifetimeSecs(86400U, 0));
  EXPECT_EQ(86100U, Policy::jwtLifetimeSecs(86400U, 1));
  EXPECT_EQ(84900U, Policy::jwtLifetimeSecs(86400U, 5));
}

TEST(MQTTConnectionPolicy, ShortJwtLifetimeUsesFivePercentPerSlot) {
  EXPECT_EQ(3300U, Policy::jwtLifetimeSecs(3300U, 0));
  EXPECT_EQ(3135U, Policy::jwtLifetimeSecs(3300U, 1));
  EXPECT_EQ(2970U, Policy::jwtLifetimeSecs(3300U, 2));
  EXPECT_EQ(85U, Policy::jwtLifetimeSecs(100U, 3));
}

TEST(MQTTConnectionPolicy, JwtLifetimeCannotUnderflowForUnexpectedSlotInput) {
  EXPECT_EQ(0U, Policy::jwtLifetimeSecs(100U, 255));
}

TEST(MQTTConnectionPolicy, RenewalBufferHasOneMinuteFloorAndFiveMinuteCap) {
  EXPECT_EQ(60U, Policy::renewalBufferSecs(0U));
  EXPECT_EQ(60U, Policy::renewalBufferSecs(599U));
  EXPECT_EQ(60U, Policy::renewalBufferSecs(600U));
  EXPECT_EQ(299U, Policy::renewalBufferSecs(2999U));
  EXPECT_EQ(300U, Policy::renewalBufferSecs(3000U));
  EXPECT_EQ(300U, Policy::renewalBufferSecs(86400U));
}

TEST(MQTTConnectionPolicy, UnsynchronizedClockOnlyCreatesAMissingToken) {
  EXPECT_TRUE(Policy::tokenNeedsRenewal(false, 0U, 0U, 300U));
  EXPECT_FALSE(Policy::tokenNeedsRenewal(false, 0U, 1735693200U, 300U));
}

TEST(MQTTConnectionPolicy, SyncedClockRenewsInvalidExpiredOrImminentTokens) {
  const uint32_t expires = 1735693200U;
  EXPECT_TRUE(Policy::tokenNeedsRenewal(true, 1735689000U, 0U, 300U));
  EXPECT_TRUE(Policy::tokenNeedsRenewal(true, 1735689000U, 999999999U, 300U));
  EXPECT_FALSE(Policy::tokenNeedsRenewal(true, expires - 301U, expires, 300U));
  EXPECT_TRUE(Policy::tokenNeedsRenewal(true, expires - 300U, expires, 300U));
  EXPECT_TRUE(Policy::tokenNeedsRenewal(true, expires, expires, 300U));
  EXPECT_TRUE(Policy::tokenNeedsRenewal(true, expires + 1U, expires, 300U));
}

TEST(MQTTConnectionPolicy, RenewalThrottleHasExactBoundaryAndHandlesRollover) {
  EXPECT_FALSE(Policy::renewalAttemptAllowed(59999U, 0U));
  EXPECT_TRUE(Policy::renewalAttemptAllowed(60000U, 0U));

  const uint32_t last = std::numeric_limits<uint32_t>::max() - 29999U;
  EXPECT_FALSE(Policy::renewalAttemptAllowed(29999U, last));
  EXPECT_TRUE(Policy::renewalAttemptAllowed(30000U, last));
}

TEST(MQTTConnectionPolicy, JwtClockNeedsNtpOrAReasonableWallClock) {
  EXPECT_FALSE(Policy::jwtClockAvailable(false, Policy::kJwtClockThreshold - 1U));
  EXPECT_TRUE(Policy::jwtClockAvailable(false, Policy::kJwtClockThreshold));
  EXPECT_TRUE(Policy::jwtClockAvailable(true, 0U));
}

TEST(MQTTConnectionPolicy, WifiBackoffLadderStartsAtFifteenSecondsAndSaturates) {
  EXPECT_EQ(15000U, Policy::wifiReconnectBackoffMs(0));
  EXPECT_EQ(30000U, Policy::wifiReconnectBackoffMs(1));
  EXPECT_EQ(60000U, Policy::wifiReconnectBackoffMs(2));
  EXPECT_EQ(120000U, Policy::wifiReconnectBackoffMs(3));
  EXPECT_EQ(300000U, Policy::wifiReconnectBackoffMs(4));
  // Clamps at the 300 s rung for the saturated attempt count and beyond.
  EXPECT_EQ(300000U, Policy::wifiReconnectBackoffMs(5));
  EXPECT_EQ(300000U, Policy::wifiReconnectBackoffMs(200));
}

TEST(MQTTConnectionPolicy, WifiBackoffAttemptClimbsThenSaturatesAtFive) {
  uint8_t attempt = 0;
  for (uint8_t expected = 1; expected <= 5; ++expected) {
    attempt = Policy::nextWifiBackoffAttempt(attempt);
    EXPECT_EQ(expected, attempt);
  }
  // Saturated: never advances past 5 (index stays clamped at the 300 s rung).
  EXPECT_EQ(5U, Policy::nextWifiBackoffAttempt(attempt));
  EXPECT_EQ(5U, Policy::nextWifiBackoffAttempt(5));
}

TEST(MQTTConnectionPolicy, WifiReconnectRequiresBothDownAndSinceAttemptToClearRung) {
  const uint32_t down_since = 1000U;
  const uint32_t last_attempt = 1000U;
  const uint8_t attempt = 0;  // 15 s rung
  // Neither interval has elapsed yet.
  EXPECT_FALSE(Policy::wifiReconnectDue(1000U + 14999U, down_since, last_attempt, attempt));
  // Down long enough, but an attempt was made only 5 s ago (since-attempt short).
  EXPECT_FALSE(Policy::wifiReconnectDue(1000U + 15000U, down_since, 1000U + 10000U, attempt));
  // Both cleared at the exact boundary: due.
  EXPECT_TRUE(Policy::wifiReconnectDue(1000U + 15000U, down_since, last_attempt, attempt));
}

TEST(MQTTConnectionPolicy, WifiReconnectDueSurvivesMillisRollover) {
  const uint32_t down_since = std::numeric_limits<uint32_t>::max() - 100U;
  const uint32_t last_attempt = down_since;
  const uint8_t attempt = 0;  // 15 s rung
  const uint32_t now = down_since + 15000U;  // wraps past zero
  EXPECT_TRUE(Policy::wifiReconnectDue(now, down_since, last_attempt, attempt));
  EXPECT_FALSE(Policy::wifiReconnectDue(down_since + 14999U, down_since, last_attempt, attempt));
}

// --- classifySlotActivation: the "will this slot connect on this hardware" rule
// the CLI uses to warn at `set mqttN.preset` time. Non-PSRAM Heltec V3 is the
// motivating case: RUNTIME_MQTT_SLOTS=3 but only 2 concurrent connections. ---

using Policy::SlotActivation;

TEST(SlotActivation, NonPsramV3FirstTwoConnectThirdIsOverCap) {
  // slot_count = 3 (runtime array), max_active = 2 (concurrent cap).
  const bool enabled[6] = {true, true, true, false, false, false};
  EXPECT_EQ(SlotActivation::Connects,      Policy::classifySlotActivation(0, enabled, 3, 2));
  EXPECT_EQ(SlotActivation::Connects,      Policy::classifySlotActivation(1, enabled, 3, 2));
  // slot 3 (index 2) is the case in the field log: iterated, but skipped.
  EXPECT_EQ(SlotActivation::OverActiveCap, Policy::classifySlotActivation(2, enabled, 3, 2));
}

TEST(SlotActivation, NonPsramSlotsBeyondRuntimeArrayAreNeverIterated) {
  const bool enabled[6] = {true, true, true, true, false, false};
  // mqtt4/5/6 (index 3-5) are outside the 3-slot runtime array on non-PSRAM.
  EXPECT_EQ(SlotActivation::BeyondArray, Policy::classifySlotActivation(3, enabled, 3, 2));
  EXPECT_EQ(SlotActivation::BeyondArray, Policy::classifySlotActivation(5, enabled, 3, 2));
}

TEST(SlotActivation, RankCountsOnlyEnabledLowerSlots) {
  // Only slot index 2 enabled (1 and 2 disabled): it is the first enabled slot,
  // so it connects even though its index equals the cap.
  const bool enabled[6] = {false, false, true, false, false, false};
  EXPECT_EQ(SlotActivation::Connects, Policy::classifySlotActivation(2, enabled, 3, 2));

  // Gap in the middle: slots 0 and 2 enabled, slot 1 off. Slot 2 is rank 2 <= 2.
  const bool enabled2[6] = {true, false, true, false, false, false};
  EXPECT_EQ(SlotActivation::Connects, Policy::classifySlotActivation(2, enabled2, 3, 2));
}

TEST(SlotActivation, PsramFiveOfSixConnect) {
  // PSRAM: slot_count = 6, max_active = 5. Sixth enabled slot is over the cap.
  const bool enabled[6] = {true, true, true, true, true, true};
  EXPECT_EQ(SlotActivation::Connects,      Policy::classifySlotActivation(4, enabled, 6, 5));
  EXPECT_EQ(SlotActivation::OverActiveCap, Policy::classifySlotActivation(5, enabled, 6, 5));
}

TEST(SlotActivation, DisabledAndOutOfRangeSlots) {
  const bool enabled[6] = {true, false, true, false, false, false};
  EXPECT_EQ(SlotActivation::Disabled, Policy::classifySlotActivation(1, enabled, 3, 2));
  EXPECT_EQ(SlotActivation::Disabled, Policy::classifySlotActivation(-1, enabled, 3, 2));
  EXPECT_EQ(SlotActivation::Disabled, Policy::classifySlotActivation(0, nullptr, 3, 2));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
