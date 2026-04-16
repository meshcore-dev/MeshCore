#include "RepeaterClockSync.h"

RepeaterClockSync::RepeaterClockSync() {
  reset();
}

void RepeaterClockSync::reset() {
  _candidate_count = 0;
}

bool RepeaterClockSync::considerAdvert(const uint8_t pub_key[PUB_KEY_SIZE], uint32_t advert_timestamp,
                                       uint32_t heard_millis, uint32_t local_time, uint32_t& sync_time) {
#if REPEATER_CLOCK_SYNC_ENABLED
  if (local_time >= REPEATER_CLOCK_SYNC_STALE_BEFORE || advert_timestamp < REPEATER_CLOCK_SYNC_STALE_BEFORE) {
    return false;
  }

  RepeaterClockSyncCandidate* candidate = NULL;
  for (uint8_t i = 0; i < _candidate_count; i++) {
    if (memcmp(pub_key, _candidates[i].pub_key, PUB_KEY_SIZE) == 0) {
      candidate = &_candidates[i];
      break;
    }
  }

  if (candidate == NULL) {
    if (_candidate_count < REPEATER_CLOCK_SYNC_MAX_CANDIDATES) {
      candidate = &_candidates[_candidate_count++];
    } else {
      candidate = &_candidates[0];
      for (uint8_t i = 1; i < REPEATER_CLOCK_SYNC_MAX_CANDIDATES; i++) {
        if ((int32_t)(_candidates[i].heard_millis - candidate->heard_millis) < 0) {
          candidate = &_candidates[i];
        }
      }
    }
  }

  memcpy(candidate->pub_key, pub_key, PUB_KEY_SIZE);
  candidate->advert_timestamp = advert_timestamp;
  candidate->heard_millis = heard_millis;

  uint32_t times[REPEATER_CLOCK_SYNC_MAX_CANDIDATES];
  uint8_t count = 0;
  for (uint8_t i = 0; i < _candidate_count; i++) {
    RepeaterClockSyncCandidate* c = &_candidates[i];
    uint32_t age_millis = heard_millis - c->heard_millis;
    if (age_millis > ((uint32_t)REPEATER_CLOCK_SYNC_WINDOW_SECS) * 1000UL) {
      continue;
    }
    times[count++] = c->advert_timestamp + age_millis / 1000UL;
  }

  if (count < REPEATER_CLOCK_SYNC_QUORUM) {
    return false;
  }

  for (uint8_t i = 1; i < count; i++) {
    uint32_t v = times[i];
    int j = i - 1;
    while (j >= 0 && times[j] > v) {
      times[j + 1] = times[j];
      j--;
    }
    times[j + 1] = v;
  }

  uint8_t cluster_start = count;
  for (uint8_t i = 0; i + REPEATER_CLOCK_SYNC_QUORUM <= count; i++) {
    uint8_t cluster_end = i + REPEATER_CLOCK_SYNC_QUORUM - 1;
    if (times[cluster_end] - times[i] <= REPEATER_CLOCK_SYNC_MAX_SPREAD_SECS) {
      cluster_start = i;
      break;
    }
  }

  if (cluster_start == count) {
    return false;
  }

  sync_time = times[cluster_start + REPEATER_CLOCK_SYNC_QUORUM / 2];
  return sync_time > local_time;
#else
  (void)pub_key;
  (void)advert_timestamp;
  (void)heard_millis;
  (void)local_time;
  (void)sync_time;
  return false;
#endif
}
