#pragma once

#include "TimeSyncAuth.h"

// Runtime-only counters for an authenticated time-sync consumer. These are not
// persisted, so receiving time beacons does not create flash wear.
struct TimeSyncStats {
  uint32_t received;
  uint32_t accepted;
  uint32_t display_name_mismatch;
  uint32_t malformed;
  uint32_t signature_invalid;
  uint32_t stale_timestamp;
  uint32_t replayed_sequence;
  uint32_t excessive_forward_step;
  uint32_t clock_updates;
};

// Replay cursors are per authority slot so independent senders cannot block
// each other by using separate sequence streams.
struct TimeSyncReplayState {
  uint32_t last_timestamp;
  uint16_t last_sequence;
  bool accepted_this_boot;
};

class TimeSyncConsumer {
  static bool isWrappedSequenceNewer(uint16_t previous, uint16_t current);

public:
  static void resetReplay(TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES], uint8_t& last_source,
                          bool& clock_accepted_this_boot);
  static void resetReplay(TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES], uint8_t source_idx);

  static bool applyClock(TimeSyncStats& stats, bool& clock_accepted_this_boot, uint32_t current_time,
                         uint32_t candidate_timestamp, uint32_t max_forward_step, uint32_t& new_time);

  static bool handleResult(TimeSyncStats& stats, TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES],
                           uint8_t& last_source, bool& clock_accepted_this_boot, TimeSyncResult result,
                           const TimeSyncMessage& msg, uint32_t current_time, uint32_t max_forward_step,
                           uint32_t& new_time);

  static bool consumeGroupData(TimeSyncStats& stats, TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES],
                               uint8_t& last_source, bool& clock_accepted_this_boot,
                               const TimeSyncConfigView& config, uint8_t packet_type,
                               const uint8_t* data, size_t len, uint32_t current_time,
                               uint32_t max_forward_step, uint32_t& new_time);

  static int searchChannelByHash(const TimeSyncConfigView& config, const uint8_t* hash,
                                 mesh::GroupChannel channels[], int max_matches);
};
