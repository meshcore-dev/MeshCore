/*
 * PEER-BASED TIME SYNCHRONIZATION SYSTEM
 *
 * This module implements automatic time synchronization for mesh network nodes without
 * hardware RTC, using timestamps from nearby nodes' advertisement packets.
 *
 * == HOW IT WORKS ==
 *
 * 1. Priority System:
 *    - If hardware RTC is present: Use hardware RTC only, no peer sync
 *    - If no hardware RTC: Sync using median time from nearby nodes
 *
 * 2. Timestamp Collection:
 *    - Collects timestamps from received advertisement packets
 *    - Filters by hop count (default: max 20 hops for reliability)
 *    - Records collection time (RTC time) for each timestamp (sleep-safe)
 *    - Adjusts timestamps by elapsed time before using them (accounts for clock ticking)
 *    - Adjusts timestamps by hop count and estimated mesh packet air time (e.g. total estimated air time)
 *      * Adjustment: timestamp + seconds((hop count + 1) * estimated mesh packet air time)
 *    - Adaptive validation:
 *      * BEFORE first successful sync: accepts timestamps from May 2024 to ~May 2034 range
 *        Trusts that if multiple peers agree, they're correct (even if years ahead)
 *      * AFTER N successful syncs (default N=1): strict 24-hour validation
 *        Rejects timestamps more than 24 hours from current time
 *    - Maintains a rolling buffer of recent timestamps
 *
 * 3. Statistical Outlier Filtering + Weighted Median:
 *    - Collects PEER_SYNC_SAMPLE_SIZE samples (default: 21) without filtering
 *    - Once buffer is full, applies MAD (Median Absolute Deviation) to detect outliers:
 *      * Adjusts each timestamp by adding elapsed seconds since collection
 *      * Calculate initial median of all samples
 *      * Calculate MAD = median(|timestamp - median|)
 *      * Filter out timestamps beyond median ± 3×MAD (robust threshold)
 *      * Minimum threshold of 60 seconds to handle legitimate variation
 *      * Removes detected outliers from buffer permanently
 *    - Checks if enough good samples remain (default: at least 15 of 21 = ~70%)
 *    - If enough good samples, calculates WEIGHTED median of the filtered samples
 *    - Weights by hop count: closer peers = more influence
 *    - Weight formula: (MAX_HOP_COUNT + 1 - actual_hop_count)
 *    - Example with MAX=20: 1-hop gets weight 20, 2-hop gets weight 19, ..., 20-hop gets weight 1
 *    - This approach is robust to malicious/incorrect timestamps while favoring nearby sources
 *
 * 4. Clock Update Logic:
 *    - Only updates if median is AHEAD by at least threshold (default: 10 minutes)
 *    - Prevents frequent updates (minimum 5 minutes between syncs)
 *    - Clears sample buffer after successful sync
 *
 * == CONFIGURATION ==
 *
 * Constants can be defined before including this header:
 * - PEER_SYNC_MAX_HOP_COUNT: Maximum hops for time sync (default: 20)
 *   Lower = more reliable but less coverage, Higher = more coverage but less reliable
 * - PEER_SYNC_MIN_OFFSET_SECONDS: Minimum offset to trigger sync (default: 300s / 5 min)
 * - PEER_SYNC_SAMPLE_SIZE: Timestamps to collect before filtering (default: 21, odd for median)
 * - PEER_SYNC_MIN_SAMPLES_AFTER_FILTERING: Minimum good samples after filtering (default: 15)
 *   Ensures at least ~70% of samples are good before trusting the result
 * - PEER_SYNC_MIN_SYNCS_BEFORE_STRICT_VALIDATION: Successful syncs before 24h rule (default: 1)
 * - PEER_SYNC_PAUSE_DURATION_SECONDS: Pause peer sync when clock accurate (default: 86400s / 24h)
 *   When clock is already accurate, pauses timestamp collection to reduce CPU overhead
 *
 * == USAGE ==
 *
 * Example initialization:
 *   VolatileRTCClock volatile_rtc;
 *   AutoDiscoverRTCClock auto_rtc(volatile_rtc);
 *   PeerSyncRTCClock peer_sync_rtc(auto_rtc);
 *
 *   // After I2C init (hardware RTC detection happens here)
 *   auto_rtc.begin(Wire);
 *
 *   // Pass to mesh
 *   mesh::Mesh mesh(..., peer_sync_rtc, ...);
 *
 * The mesh network automatically:
 * - Collects timestamps from advertisements
 * - Checks if hardware RTC present via hasHardwareRTC()
 * - Only syncs from peers if no hardware RTC detected
 */

#include "PeerSyncRTCClock.h"
#include <time.h>

// Timestamp validation bounds for lenient mode (before first successful sync)
// Lower bound: Base time when firmware was developed (May 15, 2024)
// Upper bound: ~10 years from base time (around 2034)
#define MIN_VALID_TIMESTAMP 1715770351  // May 15, 2024 10:52:31 UTC
#define MAX_VALID_TIMESTAMP 2031346351  // ~May 2034 (base + 10 years)

// Helper function to format timestamp as UTC string
static void formatUTC(uint32_t timestamp, char* buffer, size_t size) {
  time_t t = timestamp;
  struct tm* tm_info = gmtime(&t);
  strftime(buffer, size, "%Y-%m-%d %H:%M:%S UTC", tm_info);
}

bool PeerSyncRTCClock::validateTimestamp(uint32_t timestamp, uint32_t current_time) {
  int32_t diff = (int32_t)(timestamp - current_time);

  // Apply strict 24-hour validation AFTER clock has been successfully synced at least once
  // Before that, use lenient validation (trust that if multiple peers agree, they're right)
  if (_successful_sync_count >= PEER_SYNC_MIN_SYNCS_BEFORE_STRICT_VALIDATION) {
    // Strict validation: reject timestamps more than 24 hours away
    if (diff < -86400 || diff > 86400) {
      MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Timestamp rejected (strict mode, out of 24h range): diff=%d",
                         diff);
      return false;
    }
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Timestamp validation passed (strict 24h mode)");
    return true;
  } else {
    // Lenient validation for initial sync: only reject obviously invalid timestamps
    if (timestamp < MIN_VALID_TIMESTAMP || timestamp > MAX_VALID_TIMESTAMP) {
      char utc_buffer[32];
      formatUTC(timestamp, utc_buffer, sizeof(utc_buffer));
      MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Timestamp rejected (initial mode, out of valid range): timestamp=%u (%s)",
                         timestamp, utc_buffer);
      return false;
    }
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Timestamp validation passed (initial mode, syncs=%d/%d)",
                       _successful_sync_count, PEER_SYNC_MIN_SYNCS_BEFORE_STRICT_VALIDATION);
    return true;
  }
}

void PeerSyncRTCClock::addPeerTimestamp(uint32_t timestamp, uint8_t hop_count, uint32_t estimated_airtime_per_hop_ms) {
  MESH_DEBUG_PRINTLN("PeerSyncRTCClock::addPeerTimestamp called: timestamp=%u, hop_count=%d, airtime_per_hop=%ums",
                     timestamp, hop_count, estimated_airtime_per_hop_ms);

  // If we have a hardware RTC, don't sync from peers
  if (hasHardwareRTC()) {
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Hardware RTC detected, ignoring peer timestamp");
    return;
  }

  // If peer sync is paused (clock is already accurate), don't collect timestamps
  uint32_t current_time = getCurrentTime();
  if (_paused_until > 0 && current_time < _paused_until) {
    uint32_t remaining_seconds = _paused_until - current_time;
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Peer sync paused for %u more seconds (clock accurate)", remaining_seconds);
    return;
  }

  // Only accept timestamps from packets within hop count threshold
  if (hop_count > PEER_SYNC_MAX_HOP_COUNT) {
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Hop count %d exceeds max %d, rejecting", hop_count, PEER_SYNC_MAX_HOP_COUNT);
    return;
  }

  int32_t diff = (int32_t)(timestamp - current_time);

  MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Current time: %u, Peer timestamp: %u, Diff: %d seconds",
                     current_time, timestamp, diff);

  // Validate timestamp using adaptive validation (strict after first sync, lenient before)
  if (!validateTimestamp(timestamp, current_time)) {
    return;
  }

  // Compensate for transmission airtime: each hop requires time to transmit
  // The timestamp represents when the packet was SENT by the originator, but it took time to reach us
  // estimated_airtime_per_hop_ms is the airtime per hop in milliseconds (calculated from radio settings)
  // Note: hop_count + 1 because even 0 hops means 1 transmission (sender → us directly)
  uint32_t total_airtime_seconds = ((hop_count + 1) * estimated_airtime_per_hop_ms) / 1000;
  uint32_t adjusted_timestamp = timestamp + total_airtime_seconds;

  char adjusted_utc[32];
  formatUTC(adjusted_timestamp, adjusted_utc, sizeof(adjusted_utc));
  MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Compensating for airtime: %d hops (+1 transmission) × %ums = %us, adjusted %u → %u (%s)",
                     hop_count, estimated_airtime_per_hop_ms, total_airtime_seconds, timestamp, adjusted_timestamp, adjusted_utc);

  // Add adjusted timestamp to buffer with collection time (RTC time, not millis, for sleep-safe operation)
  uint32_t collection_time = current_time;  // Use RTC time so age adjustment works even after CPU sleep
  if (_sample_count < PEER_SYNC_SAMPLE_SIZE) {
    _timestamps[_sample_count] = adjusted_timestamp;  // Store adjusted, not original
    _hop_counts[_sample_count] = hop_count;
    _sample_times[_sample_count] = collection_time;
    _sample_count++;
  } else {
    // Buffer full, shift and add new sample
    for (int i = 0; i < PEER_SYNC_SAMPLE_SIZE - 1; i++) {
      _timestamps[i] = _timestamps[i + 1];
      _hop_counts[i] = _hop_counts[i + 1];
      _sample_times[i] = _sample_times[i + 1];
    }
    _timestamps[PEER_SYNC_SAMPLE_SIZE - 1] = adjusted_timestamp;  // Store adjusted, not original
    _hop_counts[PEER_SYNC_SAMPLE_SIZE - 1] = hop_count;
    _sample_times[PEER_SYNC_SAMPLE_SIZE - 1] = collection_time;
  }

  MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Timestamp accepted, sample_count=%d", _sample_count);

  #if MESH_DEBUG
  // Log current status and weighted median estimate (simple estimate during collection)
  // Wrapped in MESH_DEBUG to avoid expensive computation in release builds
  if (_sample_count < PEER_SYNC_SAMPLE_SIZE) {
    // During collection: show simple weighted median without MAD filtering
    uint32_t median_estimate = calculateSimpleWeightedMedian();
    int32_t drift = (int32_t)(median_estimate - current_time);

    char utc_buffer[32];
    formatUTC(median_estimate, utc_buffer, sizeof(utc_buffer));

    // Format drift in human-readable format
    char drift_str[64];
    uint32_t abs_drift = (drift < 0) ? -drift : drift;
    if (abs_drift < 60) {
      snprintf(drift_str, sizeof(drift_str), "%+d seconds", drift);
    } else if (abs_drift < 3600) {
      snprintf(drift_str, sizeof(drift_str), "%+d minutes, %d seconds",
               drift / 60, (int)(abs_drift % 60));
    } else if (abs_drift < 86400) {
      snprintf(drift_str, sizeof(drift_str), "%+d hours, %d minutes",
               drift / 3600, (int)((abs_drift % 3600) / 60));
    } else {
      snprintf(drift_str, sizeof(drift_str), "%+d days, %d hours",
               drift / 86400, (int)((abs_drift % 86400) / 3600));
    }

    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Collecting timestamps (%d of %d needed samples)",
                       _sample_count, PEER_SYNC_SAMPLE_SIZE);
    MESH_DEBUG_PRINTLN("  Weighted median estimate: %u (%s), drift: %s",
                       median_estimate, utc_buffer, drift_str);
  }
  #endif

  // Try to sync if we have enough samples
  if (_sample_count >= PEER_SYNC_SAMPLE_SIZE) {
    // Apply MAD filtering to remove outliers from the full sample buffer
    // This gives MAD the full statistical power of all samples
    removeOutliersFromBuffer();

    // Check if we have enough good samples after filtering
    if (_sample_count < PEER_SYNC_MIN_SAMPLES_AFTER_FILTERING) {
      MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Only %d of %d samples remain after filtering (need %d minimum)",
                         _sample_count, PEER_SYNC_SAMPLE_SIZE, PEER_SYNC_MIN_SAMPLES_AFTER_FILTERING);
      return;
    }

    // Now attempt sync with filtered samples (at least 15 good samples)
    attemptClockSync();
  }
}

// Helper function to calculate simple median (for MAD calculation)
static uint32_t calculateSimpleMedian(uint32_t* values, uint8_t count) {
  // Sort values
  for (uint8_t i = 0; i < count - 1; i++) {
    for (uint8_t j = 0; j < count - i - 1; j++) {
      if (values[j] > values[j + 1]) {
        uint32_t temp = values[j];
        values[j] = values[j + 1];
        values[j + 1] = temp;
      }
    }
  }
  return values[count / 2];
}

void PeerSyncRTCClock::adjustTimestampsForAge(uint32_t adjusted_out[], uint8_t count) {
  uint32_t current_rtc_time = getCurrentTime();
  for (uint8_t i = 0; i < count; i++) {
    // Calculate elapsed time using RTC time (works even if CPU was sleeping)
    uint32_t elapsed_seconds = current_rtc_time - _sample_times[i];
    adjusted_out[i] = _timestamps[i] + elapsed_seconds;
  }
}

uint32_t PeerSyncRTCClock::calculateMADThreshold(uint32_t adjusted_timestamps[], uint8_t count, uint32_t& simple_median_out) {
  // Calculate simple median
  uint32_t temp_timestamps[PEER_SYNC_SAMPLE_SIZE];
  memcpy(temp_timestamps, adjusted_timestamps, count * sizeof(uint32_t));
  simple_median_out = calculateSimpleMedian(temp_timestamps, count);

  // Calculate MAD (Median Absolute Deviation)
  uint32_t absolute_deviations[PEER_SYNC_SAMPLE_SIZE];
  for (uint8_t i = 0; i < count; i++) {
    int32_t deviation = (int32_t)(adjusted_timestamps[i] - simple_median_out);
    absolute_deviations[i] = (deviation < 0) ? -deviation : deviation;
  }
  uint32_t mad = calculateSimpleMedian(absolute_deviations, count);

  // Calculate threshold: 3×MAD with minimum of 60 seconds
  uint32_t threshold = mad * 3;
  if (threshold < 60) {
    threshold = 60;
  }

  return threshold;
}

uint32_t PeerSyncRTCClock::calculateWeightedMedianFromArrays(uint32_t sorted_timestamps[], uint8_t sorted_hop_counts[], uint8_t count) {
  if (count == 0) return 0;

  // Calculate weights: closer peers (fewer hops) = higher weight
  uint32_t total_weight = 0;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t weight = PEER_SYNC_MAX_HOP_COUNT + 1 - sorted_hop_counts[i];
    if (weight < 1) weight = 1;
    total_weight += weight;
  }

  // Find weighted median position
  uint32_t target_weight = total_weight / 2;
  uint32_t cumulative_weight = 0;

  for (uint8_t i = 0; i < count; i++) {
    uint8_t weight = PEER_SYNC_MAX_HOP_COUNT + 1 - sorted_hop_counts[i];
    if (weight < 1) weight = 1;
    cumulative_weight += weight;

    if (cumulative_weight >= target_weight) {
      MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Weighted median from %d samples (total weight: %u, position: %d, hops: %d)",
                         count, total_weight, i, sorted_hop_counts[i]);
      return sorted_timestamps[i];
    }
  }

  // Fallback: return middle element
  return sorted_timestamps[count / 2];
}

uint32_t PeerSyncRTCClock::calculateSimpleWeightedMedian() {
  if (_sample_count == 0) {
    return 0;
  }

  // Adjust timestamps for elapsed time since collection
  uint32_t adjusted_timestamps[PEER_SYNC_SAMPLE_SIZE];
  adjustTimestampsForAge(adjusted_timestamps, _sample_count);

  // Sort timestamps and hop counts together
  uint32_t sorted_timestamps[PEER_SYNC_SAMPLE_SIZE];
  uint8_t sorted_hop_counts[PEER_SYNC_SAMPLE_SIZE];
  memcpy(sorted_timestamps, adjusted_timestamps, _sample_count * sizeof(uint32_t));
  memcpy(sorted_hop_counts, _hop_counts, _sample_count * sizeof(uint8_t));

  // Bubble sort (simple for small arrays)
  for (uint8_t i = 0; i < _sample_count - 1; i++) {
    for (uint8_t j = 0; j < _sample_count - i - 1; j++) {
      if (sorted_timestamps[j] > sorted_timestamps[j + 1]) {
        uint32_t temp_ts = sorted_timestamps[j];
        sorted_timestamps[j] = sorted_timestamps[j + 1];
        sorted_timestamps[j + 1] = temp_ts;

        uint8_t temp_hc = sorted_hop_counts[j];
        sorted_hop_counts[j] = sorted_hop_counts[j + 1];
        sorted_hop_counts[j + 1] = temp_hc;
      }
    }
  }

  // Calculate weighted median without any filtering
  return calculateWeightedMedianFromArrays(sorted_timestamps, sorted_hop_counts, _sample_count);
}

void PeerSyncRTCClock::removeOutliersFromBuffer() {
  if (_sample_count == 0) return;

  // This method is only called when buffer is full (sample_count >= PEER_SYNC_SAMPLE_SIZE)
  // At this point we have enough samples for reliable MAD-based outlier detection

  // Adjust timestamps and calculate MAD threshold
  uint32_t adjusted_timestamps[PEER_SYNC_SAMPLE_SIZE];
  adjustTimestampsForAge(adjusted_timestamps, _sample_count);

  uint32_t simple_median;
  uint32_t outlier_threshold = calculateMADThreshold(adjusted_timestamps, _sample_count, simple_median);

  // Compact the sample arrays by removing outliers
  uint8_t write_index = 0;
  uint8_t outliers_removed = 0;

  for (uint8_t read_index = 0; read_index < _sample_count; read_index++) {
    int32_t deviation = (int32_t)(adjusted_timestamps[read_index] - simple_median);
    uint32_t abs_deviation = (deviation < 0) ? -deviation : deviation;

    if (abs_deviation <= outlier_threshold) {
      // Keep this sample - copy if necessary
      if (write_index != read_index) {
        _timestamps[write_index] = _timestamps[read_index];
        _hop_counts[write_index] = _hop_counts[read_index];
        _sample_times[write_index] = _sample_times[read_index];
      }
      write_index++;
    } else {
      outliers_removed++;
    }
  }

  if (outliers_removed > 0) {
    _sample_count = write_index;
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Removed %d persistent outliers from buffer, %d samples remain",
                       outliers_removed, _sample_count);
  }
}

void PeerSyncRTCClock::attemptClockSync() {
  MESH_DEBUG_PRINTLN("PeerSyncRTCClock::attemptClockSync called, sample_count=%d", _sample_count);

  // Log all collected samples for analysis (with time adjustments)
  MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Collected samples:");
  uint32_t current_rtc_time = getCurrentTime();
  uint32_t sum = 0;
  uint32_t min_ts = 0xFFFFFFFF;
  uint32_t max_ts = 0;
  for (uint8_t i = 0; i < _sample_count; i++) {
    // Calculate elapsed time using RTC time (works even if CPU was sleeping)
    uint32_t elapsed_seconds = current_rtc_time - _sample_times[i];
    uint32_t adjusted_timestamp = _timestamps[i] + elapsed_seconds;

    char utc_buffer[32];
    formatUTC(adjusted_timestamp, utc_buffer, sizeof(utc_buffer));
    MESH_DEBUG_PRINTLN("  Sample %d: timestamp=%u (%s), hops=%d, weight=%d, age=%us",
                       i + 1, adjusted_timestamp, utc_buffer, _hop_counts[i],
                       PEER_SYNC_MAX_HOP_COUNT + 1 - _hop_counts[i], elapsed_seconds);
    sum += adjusted_timestamp;
    if (adjusted_timestamp < min_ts) min_ts = adjusted_timestamp;
    if (adjusted_timestamp > max_ts) max_ts = adjusted_timestamp;
  }
  uint32_t average = sum / _sample_count;
  uint32_t spread = max_ts - min_ts;

  char min_utc[32], max_utc[32];
  formatUTC(min_ts, min_utc, sizeof(min_utc));
  formatUTC(max_ts, max_utc, sizeof(max_utc));
  MESH_DEBUG_PRINTLN("  Raw statistics: min=%u (%s), max=%u (%s), spread=%u seconds, average=%u",
                     min_ts, min_utc, max_ts, max_utc, spread, average);

  // Don't sync too frequently (wait at least 5 minutes between syncs)
  // Use RTC time for rate limiting (works even if CPU was sleeping)
  uint32_t current_time = getCurrentTime();
  if (_last_sync_time > 0) {
    uint32_t time_since_last_sync_sec = current_time - _last_sync_time;
    if (time_since_last_sync_sec < 300) {  // 300 seconds = 5 minutes
      MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Rate limited, %u seconds since last sync (need 300s)",
                         time_since_last_sync_sec);
      return;
    }
  }

  // Calculate weighted median from already-filtered buffer
  // Note: outliers were already removed from buffer by removeOutliersFromBuffer() in addPeerTimestamp()
  // So we just calculate the weighted median without filtering again
  uint32_t median_time = calculateSimpleWeightedMedian();
  if (median_time == 0) {
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: calculateSimpleWeightedMedian failed (no samples)");
    // Clear half the samples to allow fresh collection
    _sample_count = _sample_count / 2;
    return;
  }

  // Calculate offset (reuse current_time from rate limiting check above)
  int32_t offset = (int32_t)(median_time - current_time);

  char median_utc[32];
  formatUTC(median_time, median_utc, sizeof(median_utc));

  MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Offset check: offset=%d, min_required=%d",
                     offset, PEER_SYNC_MIN_OFFSET_SECONDS);
  MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Weighted median: %u (%s)", median_time, median_utc);

  // Only update if median is ahead by at least the minimum threshold
  if (offset >= PEER_SYNC_MIN_OFFSET_SECONDS) {
    char current_utc[32];
    formatUTC(current_time, current_utc, sizeof(current_utc));

    // Calculate time since last sync using RTC time (works even if CPU was sleeping)
    uint32_t time_since_last_sec = (_last_sync_time > 0) ? (current_time - _last_sync_time) : 0;

    #if MESH_DEBUG
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: *** SYNCING CLOCK ***");

    // Log time since last sync
    if (_last_sync_time == 0) {
      MESH_DEBUG_PRINTLN("  First clock sync");
    } else if (time_since_last_sec < 60) {
      MESH_DEBUG_PRINTLN("  Time since last sync: %u seconds", time_since_last_sec);
    } else if (time_since_last_sec < 3600) {
      MESH_DEBUG_PRINTLN("  Time since last sync: %u minutes", time_since_last_sec / 60);
    } else if (time_since_last_sec < 86400) {
      MESH_DEBUG_PRINTLN("  Time since last sync: %u hours, %u minutes",
                         time_since_last_sec / 3600, (time_since_last_sec % 3600) / 60);
    } else {
      MESH_DEBUG_PRINTLN("  Time since last sync: %u days, %u hours",
                         time_since_last_sec / 86400, (time_since_last_sec % 86400) / 3600);
    }

    MESH_DEBUG_PRINTLN("  Syncing clock from %d peers", _sample_count);
    MESH_DEBUG_PRINTLN("  Old time: %u (%s)", current_time, current_utc);
    MESH_DEBUG_PRINTLN("  New time: %u (%s)", median_time, median_utc);
    MESH_DEBUG_PRINTLN("  Offset: %+d seconds (%+d minutes)", offset, offset / 60);
    #endif

    setCurrentTime(median_time);

    // Increment successful sync counter
    _successful_sync_count++;

    // Clear samples after successful sync
    _sample_count = 0;

    // Resume normal peer sync monitoring after successful sync
    _paused_until = 0;

    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Clock sync complete, total_syncs=%d",
                       _successful_sync_count);
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Strict 24h validation will be %s on next sync",
                       (_successful_sync_count >= PEER_SYNC_MIN_SYNCS_BEFORE_STRICT_VALIDATION) ? "ENABLED" : "DISABLED");
  } else {
    MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Offset %d is less than minimum %d, not syncing",
                       offset, PEER_SYNC_MIN_OFFSET_SECONDS);

    // If we're in strict validation mode (clock has been synced before) and offset is small,
    // pause peer sync to reduce CPU overhead since clock is already accurate
    if (_successful_sync_count >= PEER_SYNC_MIN_SYNCS_BEFORE_STRICT_VALIDATION) {
      _paused_until = current_time + PEER_SYNC_PAUSE_DURATION_SECONDS;
      _sample_count = 0;  // Clear samples since we won't be using them

      uint32_t pause_hours = PEER_SYNC_PAUSE_DURATION_SECONDS / 3600;
      MESH_DEBUG_PRINTLN("PeerSyncRTCClock: Clock accurate, pausing peer sync for %u hours to reduce CPU overhead",
                         pause_hours);
    }
  }
}
