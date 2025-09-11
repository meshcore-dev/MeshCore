#pragma once

#include "helpers/AbstractBridge.h"
#include "helpers/SimpleMeshTables.h"

#include <RTClib.h>

/**
 * @brief Base class implementing common bridge functionality
 *
 * This class provides common functionality used by different bridge implementations
 * like packet tracking, checksum calculation, timestamping, and duplicate detection.
 *
 * Features:
 * - Fletcher-16 checksum calculation for data integrity
 * - Packet duplicate detection using SimpleMeshTables
 * - Common timestamp formatting for debug logging
 * - Shared packet management and queuing logic
 */
class BridgeBase : public AbstractBridge {
public:
  virtual ~BridgeBase() = default;

  /**
   * @brief Common magic number used by all bridge implementations for packet identification
   *
   * This magic number is placed at the beginning of bridge packets to identify
   * them as mesh bridge packets and provide frame synchronization.
   */
  static constexpr uint16_t BRIDGE_PACKET_MAGIC = 0xC03E;

  /**
   * @brief Common field sizes used by bridge implementations
   *
   * These constants define the size of common packet fields used across bridges.
   * BRIDGE_MAGIC_SIZE is used by all bridges for packet identification.
   * BRIDGE_LENGTH_SIZE is used by bridges that need explicit length fields (like RS232).
   * BRIDGE_CHECKSUM_SIZE is used by all bridges for Fletcher-16 checksums.
   */
  static constexpr uint16_t BRIDGE_MAGIC_SIZE = sizeof(BRIDGE_PACKET_MAGIC);
  static constexpr uint16_t BRIDGE_LENGTH_SIZE = sizeof(uint16_t);
  static constexpr uint16_t BRIDGE_CHECKSUM_SIZE = sizeof(uint16_t);

  /**
   * @brief Default delay in milliseconds for scheduling inbound packet processing
   *
   * It provides a buffer to prevent immediate processing conflicts in the mesh network.
   * Used in handleReceivedPacket() as: millis() + BRIDGE_DELAY
   */
  static constexpr uint16_t BRIDGE_DELAY = 500; // TODO: maybe too high ?

protected:
  /** Packet manager for allocating and queuing mesh packets */
  mesh::PacketManager *_mgr;

  /** RTC clock for timestamping debug messages */
  mesh::RTCClock *_rtc;

  /** Tracks seen packets to prevent loops in broadcast communications */
  SimpleMeshTables _seen_packets;

  /**
   * @brief Constructs a BridgeBase instance
   *
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  BridgeBase(mesh::PacketManager *mgr, mesh::RTCClock *rtc) : _mgr(mgr), _rtc(rtc) {}

  /**
   * @brief Gets formatted date/time string for logging
   *
   * Format: "HH:MM:SS - DD/MM/YYYY U"
   *
   * @return Formatted date/time string
   */
  const char *getLogDateTime();

  /**
   * @brief Calculate Fletcher-16 checksum
   *
   * Based on: https://en.wikipedia.org/wiki/Fletcher%27s_checksum
   * Used to verify data integrity of received packets
   *
   * @param data Pointer to data to calculate checksum for
   * @param len Length of data in bytes
   * @return Calculated Fletcher-16 checksum
   */
  static uint16_t fletcher16(const uint8_t *data, size_t len);

  /**
   * @brief Validate received checksum against calculated checksum
   *
   * @param data Pointer to data to validate
   * @param len Length of data in bytes
   * @param received_checksum Checksum received with data
   * @return true if checksum is valid, false otherwise
   */
  bool validateChecksum(const uint8_t *data, size_t len, uint16_t received_checksum);

  /**
   * @brief Common packet handling for received packets
   *
   * Implements the standard pattern used by all bridges:
   * - Check if packet was seen before using _seen_packets.hasSeen()
   * - Queue packet for mesh processing if not seen before
   * - Free packet if already seen to prevent duplicates
   *
   * @param packet The received mesh packet
   */
  void handleReceivedPacket(mesh::Packet *packet);
};
