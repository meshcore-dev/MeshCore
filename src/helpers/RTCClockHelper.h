#pragma once

#include <helpers/PeerSyncRTCClock.h>
#include <helpers/AutoDiscoverRTCClock.h>

/**
 * \brief Helper macro to setup RTC clock with automatic peer synchronization
 *
 * This macro creates the full RTC clock chain:
 * 1. Fallback clock (variant-specific: ESP32RTCClock, VolatileRTCClock, etc.)
 * 2. AutoDiscoverRTCClock (checks for hardware RTC modules via I2C)
 * 3. PeerSyncRTCClock (syncs with mesh peers if no hardware RTC found)
 *
 * Usage in variant's target.cpp:
 *
 *   // For ESP32-based variants:
 *   SETUP_RTC_WITH_PEER_SYNC(ESP32RTCClock, fallback_clock)
 *
 *   // For nRF52-based variants:
 *   SETUP_RTC_WITH_PEER_SYNC(VolatileRTCClock, fallback_clock)
 *
 * This creates three variables:
 * - fallback_clock: The base RTC implementation
 * - auto_rtc: Wrapper that auto-discovers hardware RTC
 * - rtc_clock: Final clock with peer sync (pass this to Mesh constructor)
 *
 * The peer sync logic only activates when no hardware RTC is present,
 * so variants with hardware RTC modules will continue to use them exclusively.
 */
#define SETUP_RTC_WITH_PEER_SYNC(fallback_type, fallback_name) \
  fallback_type fallback_name; \
  AutoDiscoverRTCClock auto_rtc(fallback_name); \
  PeerSyncRTCClock rtc_clock(auto_rtc);
