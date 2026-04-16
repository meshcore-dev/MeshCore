#pragma once

#include <stdint.h>

// --- Congestion control compile-time defines ---
// Override any of these via platformio.ini build_flags, e.g.:
//   -D GROUP_FLOOD_MAX=16 -D BUSY_ONSET=0.20f

#ifndef BUSY_ONSET
  #define BUSY_ONSET  0.15f     // busy level below which no throttling occurs
#endif
#ifndef BUSY_WINDOW_MS
  #define BUSY_WINDOW_MS  60000 // tumbling window for busy calculation (ms)
#endif

#ifndef GROUP_FLOOD_MAX
  #define GROUP_FLOOD_MAX  8    // max group reach when quiet. 0 = no group forwarding.
#endif
#ifndef GROUP_FLOOD_MID
  #define GROUP_FLOOD_MID  5    // knee point at moderate congestion
#endif
#ifndef GROUP_FLOOD_FLOOR
  #define GROUP_FLOOD_FLOOR 2   // minimum group reach under extreme congestion
#endif

#ifndef ADVERT_FLOOD_MAX
  #define ADVERT_FLOOD_MAX  8   // max advert reach when quiet. 0 = no advert forwarding.
#endif
#ifndef ADVERT_FLOOD_MID
  #define ADVERT_FLOOD_MID  4   // knee point at moderate congestion
#endif
#ifndef ADVERT_FLOOD_FLOOR
  #define ADVERT_FLOOD_FLOOR 1  // minimum advert reach under extreme congestion
#endif

// Busy score component weights (must sum to 1.0)
#ifndef BUSY_WEIGHT_AIRTIME
  #define BUSY_WEIGHT_AIRTIME  0.5f  // tx+rx airtime fraction of window
#endif
#ifndef BUSY_WEIGHT_QUEUE
  #define BUSY_WEIGHT_QUEUE    0.3f  // tx queue fill level
#endif
#ifndef BUSY_WEIGHT_DUP
  #define BUSY_WEIGHT_DUP      0.2f  // flood duplicate ratio
#endif

// TX delay scaling: delay multiplier = 1 + busy * BUSY_TX_DELAY_SCALE
// At busy=0: 1× (unchanged), at busy=1: (1+scale)× wider jitter window
#ifndef BUSY_TX_DELAY_SCALE
  #define BUSY_TX_DELAY_SCALE  2.0f  // busy=1 gives 3× wider retransmit window
#endif

/**
 * \brief  Piecewise-linear ramp: dead zone + two-segment curve through ceiling → mid → floor.
 *         Returns effective hop limit for a given busy score.
 */
inline uint8_t getEffectiveFloodMax(float busy, uint8_t ceiling, uint8_t mid, uint8_t floor) {
  if (ceiling == 0) return 0;          // type disabled
  if (mid > ceiling) mid = ceiling;    // guard against misconfigured overrides
  if (floor > mid)   floor = mid;
  if (busy <= BUSY_ONSET) return ceiling;
  if (busy >= 1.0f) return floor;

  float t = (busy - BUSY_ONSET) / (1.0f - BUSY_ONSET);  // normalize active zone to [0,1]
  if (t <= 0.5f) {
    float s = t / 0.5f;  // [0,1] within first half
    return mid + (uint8_t)((1.0f - s) * (ceiling - mid));
  } else {
    float s = (t - 0.5f) / 0.5f;  // [0,1] within second half
    return floor + (uint8_t)((1.0f - s) * (mid - floor));
  }
}

/**
 * \brief  Lightweight busy score tracker. Computes busy ∈ [0,1] over a rolling window
 *         from airtime ratio, queue depth, and flood duplicate ratio.
 *         Call update() from loop(). Recomputes every BUSY_WINDOW_MS (tumbling window).
 *         All counters come from existing Dispatcher/SimpleMeshTables.
 */
struct BusyTracker {
  float busy;           // composite score [0,1]
  float airtime_ratio;  // tx+rx airtime / window
  float queue_ratio;    // queue depth / capacity
  float dup_ratio;      // flood dups / flood recv

  // snapshot of counters at start of window
  unsigned long window_start;
  unsigned long prev_tx_air;
  unsigned long prev_rx_air;
  uint32_t prev_flood_recv;
  uint32_t prev_flood_dups;

  void reset(unsigned long now_ms,
             unsigned long tx_air, unsigned long rx_air,
             uint32_t flood_recv, uint32_t flood_dups) {
    busy = 0;
    airtime_ratio = queue_ratio = dup_ratio = 0;
    window_start = now_ms;
    prev_tx_air = tx_air;
    prev_rx_air = rx_air;
    prev_flood_recv = flood_recv;
    prev_flood_dups = flood_dups;
  }

  void update(unsigned long now_ms,
              unsigned long tx_air, unsigned long rx_air,
              int queue_count, int queue_capacity,
              uint32_t flood_recv, uint32_t flood_dups) {
    unsigned long elapsed = now_ms - window_start;
    if (elapsed < BUSY_WINDOW_MS) return;  // not yet time

    // airtime deltas — detect counter wrap (~49 days) and skip window
    unsigned long delta_tx = tx_air - prev_tx_air;
    unsigned long delta_rx = rx_air - prev_rx_air;
    if (delta_tx > elapsed || delta_rx > elapsed) {
      // counter wrapped — re-baseline and keep previous busy score
      prev_tx_air = tx_air;
      prev_rx_air = rx_air;
      prev_flood_recv = flood_recv;
      prev_flood_dups = flood_dups;
      window_start = now_ms;
      return;
    }

    // airtime component: fraction of window spent transmitting or receiving
    airtime_ratio = (float)(delta_tx + delta_rx) / (float)elapsed;
    if (airtime_ratio > 1.0f) airtime_ratio = 1.0f;

    // queue component: instantaneous depth / capacity
    queue_ratio = (queue_capacity > 0) ? (float)queue_count / (float)queue_capacity : 0;
    if (queue_ratio > 1.0f) queue_ratio = 1.0f;

    // duplicate component: flood dups / flood recv
    uint32_t delta_recv = flood_recv - prev_flood_recv;
    uint32_t delta_dups = flood_dups - prev_flood_dups;
    dup_ratio = (delta_recv > 0) ? (float)delta_dups / (float)delta_recv : 0;
    if (dup_ratio > 1.0f) dup_ratio = 1.0f;

    // weighted composite
    busy = airtime_ratio * BUSY_WEIGHT_AIRTIME + queue_ratio * BUSY_WEIGHT_QUEUE + dup_ratio * BUSY_WEIGHT_DUP;
    if (busy > 1.0f) busy = 1.0f;

    // slide window
    prev_tx_air = tx_air;
    prev_rx_air = rx_air;
    prev_flood_recv = flood_recv;
    prev_flood_dups = flood_dups;
    window_start = now_ms;
  }
};
