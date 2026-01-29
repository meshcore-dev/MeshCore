#pragma once

#include <Mesh.h>
#include <Arduino.h>

// Configuration constants
#ifndef PEER_SYNC_MAX_HOP_COUNT
#define PEER_SYNC_MAX_HOP_COUNT 20                       // Maximum hop count for time synchronization
#endif

#ifndef PEER_SYNC_MIN_OFFSET_SECONDS
#define PEER_SYNC_MIN_OFFSET_SECONDS 120                // Minimum offset (2 minutes) to trigger sync
#endif

#ifndef PEER_SYNC_SAMPLE_SIZE
#define PEER_SYNC_SAMPLE_SIZE 21                        // Minimum timestamps required before sync (odd number for median)
#endif

#ifndef PEER_SYNC_MIN_SAMPLES_AFTER_FILTERING
#define PEER_SYNC_MIN_SAMPLES_AFTER_FILTERING 15        // Minimum samples after outlier removal to trust result (~70% good samples)
#endif

#ifndef PEER_SYNC_MIN_SYNCS_BEFORE_STRICT_VALIDATION
#define PEER_SYNC_MIN_SYNCS_BEFORE_STRICT_VALIDATION 1  // Number of successful syncs before enforcing 24h validation
#endif

#ifndef PEER_SYNC_PAUSE_DURATION_SECONDS
#define PEER_SYNC_PAUSE_DURATION_SECONDS 86400          // Pause duration when clock is accurate (24 hours)
#endif

/**
 * \brief  RTCClock wrapper that synchronizes time with peers via mesh network
 *
 * This class wraps any existing RTCClock and adds peer-based time synchronization:
 * - Uses hardware RTC if available (via wrapped clock)
 * - Otherwise syncs using statistical outlier filtering + weighted median from nearby nodes
 * - Uses MAD (Median Absolute Deviation) to detect and reject outliers (3×MAD threshold)
 * - Requires minimum 70% consensus among samples after outlier filtering
 * - Weights remaining samples by hop count (closer peers = more influence)
 * - Only considers packets within max hop count threshold
 * - Only updates clock if median is ahead by minimum offset threshold
 * - Pauses peer sync when clock is accurate to reduce CPU overhead (configurable duration)
 */
class PeerSyncRTCClock : public mesh::RTCClock {
  mesh::RTCClock* _wrapped_clock;

  // Timestamp collection buffer
  uint32_t _timestamps[PEER_SYNC_SAMPLE_SIZE];
  uint8_t _hop_counts[PEER_SYNC_SAMPLE_SIZE];
  uint32_t _sample_times[PEER_SYNC_SAMPLE_SIZE];  // RTC time (Unix timestamp) when each sample was collected
  uint8_t _sample_count;
  uint32_t _last_sync_time;  // RTC time (Unix timestamp) of last successful sync
  uint8_t _successful_sync_count;
  uint32_t _paused_until;    // RTC time when to resume peer sync (0 = not paused)

  // Helper: Validate timestamp based on sync state (adaptive validation)
  // Returns true if timestamp passes validation, false otherwise
  bool validateTimestamp(uint32_t timestamp, uint32_t current_time);

  // Helper: Adjust timestamps for elapsed time since collection
  void adjustTimestampsForAge(uint32_t adjusted_out[], uint8_t count);

  // Helper: Calculate MAD threshold from adjusted timestamps (minimum 60 seconds)
  uint32_t calculateMADThreshold(uint32_t adjusted_timestamps[], uint8_t count, uint32_t& simple_median_out);

  // Helper: Calculate weighted median from sorted timestamp/hop count arrays
  uint32_t calculateWeightedMedianFromArrays(uint32_t sorted_timestamps[], uint8_t sorted_hop_counts[], uint8_t count);

  // Helper: Calculate simple weighted median without MAD filtering
  // Used both for progress estimates during collection and for final calculation after filtering
  uint32_t calculateSimpleWeightedMedian();

  // Remove detected outliers from the sample buffer using MAD filtering
  // Only called when buffer is full (>= PEER_SYNC_SAMPLE_SIZE)
  void removeOutliersFromBuffer();

  // Check if we should update the clock based on collected timestamps
  void attemptClockSync();

public:
  PeerSyncRTCClock(mesh::RTCClock& wrapped_clock)
    : _wrapped_clock(&wrapped_clock), _sample_count(0), _last_sync_time(0), _successful_sync_count(0), _paused_until(0) {
    memset(_timestamps, 0, sizeof(_timestamps));
    memset(_hop_counts, 0, sizeof(_hop_counts));
    memset(_sample_times, 0, sizeof(_sample_times));
  }

  uint32_t getCurrentTime() override {
    return _wrapped_clock->getCurrentTime();
  }

  void setCurrentTime(uint32_t time) override {
    _wrapped_clock->setCurrentTime(time);
    _last_sync_time = time;  // Store RTC time of sync, not millis()
  }

  void tick() override {
    _wrapped_clock->tick();
  }

  /**
   * \brief Add a timestamp from a received advertisement packet
   * \param timestamp  The timestamp from the packet
   * \param hop_count  Number of hops the packet has traveled (path_len)
   * \param estimated_airtime_per_hop_ms  Estimated airtime per hop in milliseconds
   */
  void addPeerTimestamp(uint32_t timestamp, uint8_t hop_count, uint32_t estimated_airtime_per_hop_ms = 1000) override;

  /**
   * \brief Check if hardware RTC is available (forwards to wrapped clock)
   */
  bool hasHardwareRTC() const override {
    return _wrapped_clock->hasHardwareRTC();
  }
};
