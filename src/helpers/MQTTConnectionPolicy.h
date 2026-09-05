#pragma once

#include <stdint.h>

// Pure timing and state-transition policy used by MQTTBridge's connection
// maintenance loop. Keeping these decisions independent of Arduino, WiFi, and
// the MQTT client lets host tests exercise the exact production policy with a
// deterministic clock.
namespace MQTTConnectionPolicy {

static const uint32_t kReconnectGuardMs = 15000UL;
static const uint32_t kStableResetMs = 120000UL;
static const uint32_t kCircuitBreakerProbeMs = 1800000UL;
static const uint32_t kRenewalThrottleMs = 60000UL;
static const uint32_t kSlotStaggerMs = 3000UL;
static const uint8_t kMaxFailuresAtMaxBackoff = 3;
static const uint32_t kDefaultJwtLifetimeSecs = 86400UL;
static const uint32_t kMaxJwtStaggerSecs = 300UL;
static const uint32_t kMinimumValidEpoch = 1000000000UL;
static const uint32_t kJwtClockThreshold = 1735689600UL; // 2025-01-01 UTC
// A wall clock at or past this instant was set from a real source (NTP or an
// admin); anything earlier is the firmware's unset-clock default (1715770351,
// 15 May 2024), so a delta spanning the sync is meaningless.
static const uint32_t kSyncedClockEpoch = kJwtClockThreshold;

// Unsigned subtraction is intentionally used: it is the standard millis()
// idiom and remains correct across a single 32-bit counter rollover.
static inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return now - then;
}

static inline bool reconnectGuardActive(uint32_t now, uint32_t last_reconnect) {
  return elapsedMs(now, last_reconnect) < kReconnectGuardMs;
}

static inline bool stableConnection(uint32_t now, uint32_t connected_at) {
  return connected_at != 0 && elapsedMs(now, connected_at) >= kStableResetMs;
}

static inline uint32_t reconnectBackoffMs(uint8_t reconnect_backoff) {
  static const uint32_t kBackoffMs[] = {
    10000UL, 30000UL, 60000UL, 120000UL, 300000UL
  };
  const uint8_t index = reconnect_backoff < 5 ? reconnect_backoff : 4;
  return kBackoffMs[index];
}

static inline uint32_t reconnectDelayMs(uint8_t reconnect_backoff, uint8_t slot_index) {
  return reconnectBackoffMs(reconnect_backoff) +
         static_cast<uint32_t>(slot_index) * kSlotStaggerMs;
}

static inline bool reconnectDue(uint32_t now, uint32_t last_attempt,
                                uint8_t reconnect_backoff, uint8_t slot_index) {
  return elapsedMs(now, last_attempt) >= reconnectDelayMs(reconnect_backoff, slot_index);
}

struct BackoffAdvance {
  uint8_t reconnect_backoff;
  uint8_t max_backoff_failures;
  bool circuit_breaker_tripped;
  bool should_reconnect;
};

// Advance the ladder immediately before a due reconnect. The first visit to
// the 300-second rung changes level 4 to the saturated marker 5. Three later
// failures at that rung trip the breaker; the third does not launch another
// connection attempt.
static inline BackoffAdvance advanceBackoff(uint8_t reconnect_backoff,
                                            uint8_t max_backoff_failures) {
  BackoffAdvance result = {
    reconnect_backoff, max_backoff_failures, false, true
  };
  if (result.reconnect_backoff < 5) {
    result.reconnect_backoff++;
    return result;
  }

  if (result.max_backoff_failures < UINT8_MAX) {
    result.max_backoff_failures++;
  }
  if (result.max_backoff_failures >= kMaxFailuresAtMaxBackoff) {
    result.circuit_breaker_tripped = true;
    result.should_reconnect = false;
  }
  return result;
}

static inline bool circuitBreakerProbeDue(uint32_t now, uint32_t last_attempt) {
  return elapsedMs(now, last_attempt) >= kCircuitBreakerProbeMs;
}

// WiFi station reconnect backoff. The bridge drives its own STA reconnect loop
// separate from the per-slot MQTT reconnects, with a slightly longer first rung
// (15 s vs the slot ladder's 10 s). Extracted from handleWiFiConnection() so the
// ladder and its wrap-safe timing are exercised by host tests instead of a
// second inline copy of the backoff math.
static inline uint32_t wifiReconnectBackoffMs(uint8_t attempt) {
  static const uint32_t kBackoffMs[] = {
    15000UL, 30000UL, 60000UL, 120000UL, 300000UL
  };
  const uint8_t index = attempt < 5 ? attempt : 4;
  return kBackoffMs[index];
}

// A reconnect is due only once the link has been down for the current rung AND
// no attempt has been made within that rung (both measured wrap-safely). This
// mirrors the two-part guard the bridge applied inline.
static inline bool wifiReconnectDue(uint32_t now, uint32_t disconnected_since,
                                    uint32_t last_attempt, uint8_t attempt) {
  const uint32_t delay = wifiReconnectBackoffMs(attempt);
  return elapsedMs(now, disconnected_since) >= delay &&
         elapsedMs(now, last_attempt) >= delay;
}

// The attempt counter climbs to 5 and then saturates; the index clamp in
// wifiReconnectBackoffMs() holds it at the 300 s rung.
static inline uint8_t nextWifiBackoffAttempt(uint8_t attempt) {
  return attempt < 5 ? static_cast<uint8_t>(attempt + 1) : attempt;
}

// Each later slot expires up to five percent of the base lifetime earlier,
// capped at five minutes per slot. Runtime slot indexes are bounded by the
// persisted MQTT slot count; the final clamp also prevents underflow if this
// helper is used with unexpected input.
static inline uint32_t jwtLifetimeSecs(uint32_t base_lifetime, uint8_t slot_index) {
  uint32_t per_slot_stagger = base_lifetime / 20UL;
  if (per_slot_stagger > kMaxJwtStaggerSecs) {
    per_slot_stagger = kMaxJwtStaggerSecs;
  }
  uint64_t stagger = static_cast<uint64_t>(slot_index) * per_slot_stagger;
  if (stagger > base_lifetime) {
    stagger = base_lifetime;
  }
  return base_lifetime - static_cast<uint32_t>(stagger);
}

static inline uint32_t renewalBufferSecs(uint32_t lifetime_secs) {
  uint32_t buffer = lifetime_secs / 10UL;
  if (buffer < 60UL) buffer = 60UL;
  if (buffer > 300UL) buffer = 300UL;
  return buffer;
}

static inline bool tokenNeedsRenewal(bool time_synced, uint32_t current_time,
                                     uint32_t token_expires_at,
                                     uint32_t renewal_buffer_secs) {
  if (!time_synced) {
    return token_expires_at == 0;
  }
  if (token_expires_at < kMinimumValidEpoch) {
    return true;
  }
  if (current_time >= token_expires_at) {
    return true;
  }
  return current_time >= token_expires_at - renewal_buffer_secs;
}

static inline bool renewalAttemptAllowed(uint32_t now, uint32_t last_attempt) {
  return elapsedMs(now, last_attempt) >= kRenewalThrottleMs;
}

static inline bool jwtClockAvailable(bool ntp_synced, uint32_t current_time) {
  return ntp_synced || current_time >= kJwtClockThreshold;
}

// How a given MQTT slot fares at bridge setup on this hardware. MQTTBridge's
// setup loop iterates runtime slots in index order and connects the first
// `max_active` *enabled* slots, skipping the rest (each WSS/TLS link needs
// ~40 KB internal heap, so non-PSRAM caps at 2 concurrent, PSRAM at 5). Slots at
// or beyond the runtime array size (`slot_count`, e.g. 3 on non-PSRAM) are never
// iterated at all. Extracted so the CLI can tell the operator, at
// `set mqttN.preset` time, whether a slot will actually come up — and so the
// exact rule is host-tested rather than hand-reasoned (it is easy to conflate
// slot_count with max_active).
enum class SlotActivation : uint8_t {
  Connects,       // enabled and within the concurrent-connection budget
  Disabled,       // slot has no preset ("none") — not attempted
  BeyondArray,    // index >= slot_count: outside the runtime slot array here
  OverActiveCap,  // enabled, but lower-numbered slots already fill the budget
};

// `enabled` must have at least `slot_count` entries; `slot` is 0-based. Mirrors
// the bridge's first-come-by-index activation order exactly.
static inline SlotActivation classifySlotActivation(int slot, const bool* enabled,
                                                    int slot_count, int max_active) {
  if (slot < 0) return SlotActivation::Disabled;
  if (slot >= slot_count) return SlotActivation::BeyondArray;
  if (enabled == nullptr || !enabled[slot]) return SlotActivation::Disabled;
  int rank = 0;  // this slot's position among enabled slots, counting by index
  for (int i = 0; i <= slot; i++) {
    if (enabled[i]) rank++;
  }
  return (rank <= max_active) ? SlotActivation::Connects
                              : SlotActivation::OverActiveCap;
}

} // namespace MQTTConnectionPolicy
