#include "TimeSyncConsumer.h"

#include <string.h>

bool TimeSyncConsumer::isWrappedSequenceNewer(uint16_t previous, uint16_t current) {
  // Treat sequence numbers as a wrapping uint16 space.
  uint16_t delta = (uint16_t)(current - previous);

  // Equal is not newer, and deltas in the upper half are older values.
  return delta != 0 && delta < 0x8000;
}

void TimeSyncConsumer::resetReplay(TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES], uint8_t& last_source,
                                   bool& clock_accepted_this_boot) {
  // Replay state is deliberately RAM-only to avoid flash wear per packet.
  for (uint8_t i = 0; i < TIME_SYNC_MAX_SOURCES; i++) {
    resetReplay(replay, i);
  }

  // The fallback-clock exception is global because only one RTC is updated.
  clock_accepted_this_boot = false;
  last_source = 0xFF;
}

void TimeSyncConsumer::resetReplay(TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES], uint8_t source_idx) {
  if (source_idx >= TIME_SYNC_MAX_SOURCES) return;

  // Clear only the requested authority slot when one source changes.
  replay[source_idx].last_timestamp = 0;
  replay[source_idx].last_sequence = 0;
  replay[source_idx].accepted_this_boot = false;
}

bool TimeSyncConsumer::applyClock(TimeSyncStats& stats, bool& clock_accepted_this_boot, uint32_t current_time,
                                  uint32_t candidate_timestamp, uint32_t max_forward_step, uint32_t& new_time) {
  // Reject timestamps outside the firmware-supported Unix range.
  if (candidate_timestamp < TIME_SYNC_MIN_UNIX_TIME || candidate_timestamp > TIME_SYNC_MAX_UNIX_TIME) {
    stats.stale_timestamp++;
    return false;
  }

  // The time-sync consumer is forward-only and never moves the clock back.
  if (candidate_timestamp <= current_time) {
    stats.stale_timestamp++;
    return false;
  }

  // VolatileRTCClock and ESP32 power-on fallback both start at this epoch.
  // Treat only the early fallback window as uninitialised, so a signed packet
  // cannot push an already-running device decades forward after long uptime.
  bool fallback_clock = !clock_accepted_this_boot
      && current_time >= TIME_SYNC_MIN_UNIX_TIME
      && current_time <= TIME_SYNC_MIN_UNIX_TIME + 86400UL;

  // A stored zero is repaired to the default so policy remains fail-closed.
  uint32_t max_step = max_forward_step;
  if (max_step == 0) max_step = TIME_SYNC_DEFAULT_MAX_FORWARD_STEP;

  // Once the clock is past the fallback window, limit authenticated jumps.
  if (!fallback_clock && candidate_timestamp - current_time > max_step) {
    stats.excessive_forward_step++;
    return false;
  }

  // Return the authenticated, policy-accepted timestamp to the caller.
  new_time = candidate_timestamp;
  clock_accepted_this_boot = true;
  stats.clock_updates++;
  return true;
}

bool TimeSyncConsumer::handleResult(TimeSyncStats& stats, TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES],
                                    uint8_t& last_source, bool& clock_accepted_this_boot, TimeSyncResult result,
                                    const TimeSyncMessage& msg, uint32_t current_time, uint32_t max_forward_step,
                                    uint32_t& new_time) {
  // Convert parser outcomes into diagnostics before any clock policy runs.
  switch (result) {
    case TIME_SYNC_OK:
      break;
    case TIME_SYNC_DISABLED:
      return false;
    case TIME_SYNC_DISPLAY_NAME_MISMATCH:
      stats.display_name_mismatch++;
      return false;
    case TIME_SYNC_SIGNATURE_INVALID:
      stats.signature_invalid++;
      return false;
    case TIME_SYNC_MALFORMED:
    default:
      stats.malformed++;
      return false;
  }

  // The parser returns the configured source slot, so guard against corrupted
  // or future message structs before indexing the replay array.
  if (msg.source_index >= TIME_SYNC_MAX_SOURCES) {
    stats.malformed++;
    return false;
  }
  TimeSyncReplayState& source_replay = replay[msg.source_index];

  if (source_replay.accepted_this_boot) {
    // Older timestamps are stale regardless of sequence value.
    if (msg.timestamp < source_replay.last_timestamp) {
      stats.stale_timestamp++;
      return false;
    }

    // Equal timestamps cannot move the RTC forward, but classify non-advancing
    // sequence values as replay diagnostics using uint16 wrap-aware ordering.
    if (msg.timestamp == source_replay.last_timestamp) {
      if (!isWrappedSequenceNewer(source_replay.last_sequence, msg.sequence)) {
        stats.replayed_sequence++;
      } else {
        stats.stale_timestamp++;
      }
      return false;
    }
  }

  // Update replay state only after the RTC policy accepts the message.
  if (!applyClock(stats, clock_accepted_this_boot, current_time, msg.timestamp, max_forward_step, new_time)) {
    return false;
  }

  // Record the last accepted authority message for later replay checks.
  source_replay.last_timestamp = msg.timestamp;
  source_replay.last_sequence = msg.sequence;
  source_replay.accepted_this_boot = true;
  last_source = msg.source_index;
  stats.accepted++;
  return true;
}

bool TimeSyncConsumer::consumeGroupData(TimeSyncStats& stats, TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES],
                                        uint8_t& last_source, bool& clock_accepted_this_boot,
                                        const TimeSyncConfigView& config, uint8_t packet_type,
                                        const uint8_t* data, size_t len, uint32_t current_time,
                                        uint32_t max_forward_step, uint32_t& new_time) {
  // Time sync is binary-only; group text messages on the same configured
  // channel are display-only companion traffic and must not affect counters.
  if (packet_type != PAYLOAD_TYPE_GRP_DATA) return false;

  // Group datagrams require a 2-byte type and 1-byte data length prefix.
  if (len < 3) {
    stats.received++;
    stats.malformed++;
    return false;
  }

  // Decode the group-datagram application type and embedded data length.
  uint16_t data_type = ((uint16_t)data[0]) | (((uint16_t)data[1]) << 8);
  uint8_t data_len = data[2];
  size_t available_len = len - 3;

  // Other datagrams may legitimately share the configured channel.
  if (data_type != TIME_SYNC_BINARY_DATA_TYPE) return false;

  // Count malformed only after the application type identifies a time-sync
  // datagram; unrelated datagrams should not pollute time-sync diagnostics.
  if (data_len > available_len) {
    stats.received++;
    stats.malformed++;
    return false;
  }

  // Parse and verify the inner Tv1 payload before considering clock policy.
  stats.received++;
  TimeSyncMessage msg;
  TimeSyncResult result = TimeSyncAuth::parseAndVerifyBinaryMulti(config, &data[3], data_len, msg);
  return handleResult(stats, replay, last_source, clock_accepted_this_boot, result, msg,
                      current_time, max_forward_step, new_time);
}

int TimeSyncConsumer::searchChannelByHash(const TimeSyncConfigView& config, const uint8_t* hash,
                                          mesh::GroupChannel channels[], int max_matches) {
  // Do not expose a candidate channel unless time sync is enabled and complete.
  if (max_matches <= 0 || !config.enabled || config.source_count == 0) return 0;

  // Match only the configured time-sync channel hash.
  if (memcmp(hash, config.channel.hash, PATH_HASH_SIZE) != 0) return 0;

  // Return the exact configured channel so Mesh.cpp can decrypt normally.
  channels[0] = config.channel;
  return 1;
}
