#pragma once

#include <stdint.h>

/**
 * \brief  Windowed percentage of "active" time over the last ~5 observed
 *         seconds, kept as a ring of five 1-second buckets of active
 *         milliseconds. Integer-only and millis-delta driven, so it is
 *         independent of the loop() call rate. ~32 bytes RAM.
 */
class WindowedPercent {
  uint32_t buckets[5];   // completed 1s buckets: active ms in each (each observed exactly 1000 ms)
  uint32_t cur_active;   // current (partial) second: active ms
  uint16_t cur_total;    // current second: observed ms (0..1000)
  uint8_t  oldest;       // index of oldest bucket (next to overwrite)
  uint8_t  filled;       // number of completed buckets (grows to 5)
  uint32_t last_ms;      // stamp of previous add()
public:
  WindowedPercent() : cur_active(0), cur_total(0), oldest(0), filled(0), last_ms(0) {
    for (int i = 0; i < 5; i++) buckets[i] = 0;
  }

  // Attribute 'active_ms' of the time elapsed since the previous call.
  void add(uint32_t now, uint32_t active_ms) {
    uint32_t dt = now - last_ms; last_ms = now;
    if (dt > 1000) dt = 1000;                 // long stall: count at most 1s of the last state
    if (active_ms > dt) active_ms = dt;
    while (dt > 0) {                          // split across the 1s bucket boundary
      uint32_t space = 1000 - cur_total;
      uint32_t take = (dt < space) ? dt : space;
      cur_total += take; dt -= take;
      uint32_t a = (active_ms < take) ? active_ms : take;
      cur_active += a; active_ms -= a;
      if (cur_total >= 1000) {                // roll into the ring
        buckets[oldest] = cur_active;
        oldest = (oldest + 1) % 5;
        if (filled < 5) filled++;
        cur_active = 0; cur_total = 0;
      }
    }
  }

  // Percent 0..100 across the observed window. The denominator is the
  // *observed* time: completed buckets observed exactly 1000 ms each.
  uint8_t pct() const {
    uint32_t num = cur_active, den = cur_total + 1000UL * filled;
    for (int i = 0; i < 5; i++) num += buckets[i];
    return (den == 0) ? 0 : (uint8_t)((num * 100) / den);
  }

  // Forget everything (stats reset). last_ms is left alone: loop()-rate
  // callers pass dt of only a few ms, so observation restarts at ~0.
  void clear() {
    for (int i = 0; i < 5; i++) buckets[i] = 0;
    cur_active = 0; cur_total = 0; oldest = 0; filled = 0;
  }
};

/**
 * \brief  Windowed ratio of "bad" discrete events over the last ~10 minutes
 *         (e.g. RX CRC failures). Same bucket-ring idea as WindowedPercent, but
 *         count-based and much longer: packet counts on a quiet mesh need
 *         minutes, not seconds, to become statistically meaningful. The
 *         time-based utilization/deafness metrics stay at ~5 s.
 */
template <uint8_t N_BUCKETS = 60, uint32_t BUCKET_MS = 10000>  // 60 x 10 s = ~10 min
class WindowedCountedRatio {
  uint16_t ev[N_BUCKETS], bad[N_BUCKETS]; // completed buckets: event / bad-event counts
  uint16_t cur_ev, cur_bad;               // current (partial) bucket
  uint32_t cur_ms;                        // ms accumulated toward the next bucket roll
  uint8_t  oldest;
  uint32_t last_ms;
  void advance(uint32_t now) {                // roll completed buckets
    uint32_t dt = now - last_ms; last_ms = now;
    uint32_t window_ms = (uint32_t)N_BUCKETS * BUCKET_MS;
    if (dt > window_ms) dt = window_ms;       // long stall/sleep: the window has slid past anyway
    cur_ms += dt;                             // accumulate: callers tick far faster than one bucket,
    // A stall spanning the whole window makes everything pre-stall older than
    // the window: drop it instead of baking it into the oldest (surviving) bucket.
    if (cur_ms / BUCKET_MS >= N_BUCKETS) { cur_ev = 0; cur_bad = 0; }
    while (cur_ms >= BUCKET_MS) {             // so no single dt ever fills a bucket by itself
      ev[oldest] = cur_ev; bad[oldest] = cur_bad;
      oldest = (oldest + 1) % N_BUCKETS;
      cur_ev = 0; cur_bad = 0;                // long stall: window just slides past
      cur_ms -= BUCKET_MS;
    }
  }
public:
  WindowedCountedRatio() : cur_ev(0), cur_bad(0), cur_ms(0), oldest(0), last_ms(0) {
    for (int i = 0; i < N_BUCKETS; i++) { ev[i] = 0; bad[i] = 0; }
  }

  // 'n_ev' counts ALL events (attempts), of which 'n_bad' failed.
  void add(uint32_t now, uint16_t n_ev, uint16_t n_bad) {
    advance(now);
    cur_ev += n_ev; cur_bad += n_bad;
  }

  // Forget everything (stats reset). last_ms is left alone: loop()-rate
  // callers pass dt of only a few ms, so post-clear observation restarts at ~0.
  void clear() {
    for (int i = 0; i < N_BUCKETS; i++) { ev[i] = 0; bad[i] = 0; }
    cur_ev = 0; cur_bad = 0; cur_ms = 0; oldest = 0;
  }

  // Window totals: all events (attempts) and the failing subset, saturated at
  // 0xFFFF. Only the good/bad RATIO is meaningful for display; the absolute
  // counts reflect what has actually been observed since construction or the
  // last clear() (they grow to full-window scale as the window fills).
  void counts(uint16_t& n_ev, uint16_t& n_bad) const {
    uint32_t e = cur_ev, b = cur_bad;
    for (int i = 0; i < N_BUCKETS; i++) { e += ev[i]; b += bad[i]; }
    n_ev = (e > 0xFFFF) ? 0xFFFF : (uint16_t)e;
    n_bad = (b > 0xFFFF) ? 0xFFFF : (uint16_t)b;
  }
};
