#pragma once

#include <stdint.h>
#include <string.h>
#include <MeshCore.h>

#ifndef REPEATER_CLOCK_SYNC_ENABLED
  #define REPEATER_CLOCK_SYNC_ENABLED 1
#endif
#ifndef REPEATER_CLOCK_SYNC_MAX_CANDIDATES
  #define REPEATER_CLOCK_SYNC_MAX_CANDIDATES 4
#endif
#ifndef REPEATER_CLOCK_SYNC_QUORUM
  #define REPEATER_CLOCK_SYNC_QUORUM 2
#endif
#ifndef REPEATER_CLOCK_SYNC_WINDOW_SECS
  #define REPEATER_CLOCK_SYNC_WINDOW_SECS 7200
#endif
#ifndef REPEATER_CLOCK_SYNC_MAX_SPREAD_SECS
  #define REPEATER_CLOCK_SYNC_MAX_SPREAD_SECS 300
#endif
#ifndef REPEATER_CLOCK_SYNC_STALE_BEFORE
  #define REPEATER_CLOCK_SYNC_STALE_BEFORE 1735689600UL  // 1 Jan 2025 00:00:00 UTC
#endif

struct RepeaterClockSyncCandidate {
  uint8_t pub_key[PUB_KEY_SIZE];
  uint32_t advert_timestamp;
  uint32_t heard_millis;
};

class RepeaterClockSync {
  RepeaterClockSyncCandidate _candidates[REPEATER_CLOCK_SYNC_MAX_CANDIDATES];
  uint8_t _candidate_count;

public:
  RepeaterClockSync();

  void reset();
  uint8_t getCandidateCount() const { return _candidate_count; }

  bool considerAdvert(const uint8_t pub_key[PUB_KEY_SIZE], uint32_t advert_timestamp,
                      uint32_t heard_millis, uint32_t local_time, uint32_t& sync_time);
};
