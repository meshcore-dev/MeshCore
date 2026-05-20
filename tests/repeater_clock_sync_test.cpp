#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../examples/simple_repeater/RepeaterClockSync.h"

static void makeKey(uint8_t pub_key[PUB_KEY_SIZE], uint8_t seed) {
  for (int i = 0; i < PUB_KEY_SIZE; i++) {
    pub_key[i] = seed + i;
  }
}

static bool consider(RepeaterClockSync& sync, uint8_t seed, uint32_t advert_time,
                     uint32_t heard_millis, uint32_t local_time, uint32_t& sync_time) {
  uint8_t key[PUB_KEY_SIZE];
  makeKey(key, seed);
  return sync.considerAdvert(key, advert_time, heard_millis, local_time, sync_time);
}

static void valid_local_clock_is_not_adjusted() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;

  assert(!consider(sync, 1, REPEATER_CLOCK_SYNC_STALE_BEFORE + 100, 0,
                   REPEATER_CLOCK_SYNC_STALE_BEFORE, sync_time));
}

static void stale_remote_time_is_ignored() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;

  assert(!consider(sync, 1, REPEATER_CLOCK_SYNC_STALE_BEFORE - 1, 0, 1715770351, sync_time));
}

static void one_candidate_is_not_enough() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;

  assert(!consider(sync, 1, REPEATER_CLOCK_SYNC_STALE_BEFORE + 100, 0, 1715770351, sync_time));
}

static void two_close_candidates_sync() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;

  assert(!consider(sync, 1, REPEATER_CLOCK_SYNC_STALE_BEFORE + 100, 0, 1715770351, sync_time));
  assert(consider(sync, 2, REPEATER_CLOCK_SYNC_STALE_BEFORE + 160, 1000, 1715770351, sync_time));
  assert(sync_time == REPEATER_CLOCK_SYNC_STALE_BEFORE + 160);
}

static void two_distant_candidates_do_not_sync() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;

  assert(!consider(sync, 1, REPEATER_CLOCK_SYNC_STALE_BEFORE + 100, 0, 1715770351, sync_time));
  assert(!consider(sync, 2, REPEATER_CLOCK_SYNC_STALE_BEFORE + REPEATER_CLOCK_SYNC_MAX_SPREAD_SECS + 102,
                   1000, 1715770351, sync_time));
}

static void sparse_candidates_are_normalized_by_elapsed_time() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;
  uint32_t first_time = REPEATER_CLOCK_SYNC_STALE_BEFORE + 1000;
  uint32_t one_hour = 60 * 60;

  assert(!consider(sync, 1, first_time, 0, 1715770351, sync_time));
  assert(consider(sync, 2, first_time + one_hour + 20, one_hour * 1000UL, 1715770351, sync_time));
  assert(sync_time == first_time + one_hour + 20);
}

static void duplicate_repeater_updates_existing_candidate() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;

  assert(!consider(sync, 1, REPEATER_CLOCK_SYNC_STALE_BEFORE + 100, 0, 1715770351, sync_time));
  assert(sync.getCandidateCount() == 1);
  assert(!consider(sync, 1, REPEATER_CLOCK_SYNC_STALE_BEFORE + 200, 1000, 1715770351, sync_time));
  assert(sync.getCandidateCount() == 1);
}

static void outlier_does_not_block_agreeing_quorum() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;
  uint32_t base = REPEATER_CLOCK_SYNC_STALE_BEFORE + 1000;

  assert(!consider(sync, 1, base, 0, 1715770351, sync_time));
  assert(!consider(sync, 2, base + 10000, 1000, 1715770351, sync_time));
  assert(consider(sync, 3, base + 90, 2000, 1715770351, sync_time));
  assert(sync_time == base + 90);
}

static void expired_candidates_are_ignored() {
  RepeaterClockSync sync;
  uint32_t sync_time = 0;
  uint32_t base = REPEATER_CLOCK_SYNC_STALE_BEFORE + 1000;

  assert(!consider(sync, 1, base, 0, 1715770351, sync_time));
  assert(!consider(sync, 2, base + REPEATER_CLOCK_SYNC_WINDOW_SECS + 1,
                   (REPEATER_CLOCK_SYNC_WINDOW_SECS + 1) * 1000UL, 1715770351, sync_time));
}

int main() {
  valid_local_clock_is_not_adjusted();
  stale_remote_time_is_ignored();
  one_candidate_is_not_enough();
  two_close_candidates_sync();
  two_distant_candidates_do_not_sync();
  sparse_candidates_are_normalized_by_elapsed_time();
  duplicate_repeater_updates_existing_candidate();
  outlier_does_not_block_agreeing_quorum();
  expired_candidates_are_ignored();

  puts("repeater_clock_sync_test: OK");
  return 0;
}
