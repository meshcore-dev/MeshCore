#pragma once

#include "helpers/AbstractBridge.h"
#include "helpers/CommonCLI.h"
#include "helpers/SimpleMeshTables.h"
#include "helpers/bridges/BridgeFrameQueue.h"

#include <RTClib.h>

/**
 * @brief Base class implementing common bridge functionality
 *
 * This class provides common functionality used by different bridge implementations
 * like packet tracking, timestamping, and duplicate detection.
 *
 * Features:
 * - Packet duplicate detection using SimpleMeshTables
 * - Common timestamp formatting for debug logging
 * - Shared packet management and queuing logic
 * - Counters shared by every bridge, behind `get bridge.rxstats`/`bridge.txstats`
 *
 * The wire framing itself lives in BridgeCodec/BridgeSerialFramer, which stay
 * free of the Arduino and mesh headers so they can be unit tested natively.
 */
class BridgeBase : public AbstractBridge {
public:
  virtual ~BridgeBase() = default;

  /**
   * @brief Gets the current state of the bridge.
   *
   * @return true if the bridge is initialized and running, false otherwise.
   */
  bool isRunning() const override;

protected:
  /** Tracks bridge state */
  bool _initialized = false;

  /** Packet manager for allocating and queuing mesh packets */
  mesh::PacketManager *_mgr;

  /** RTC clock for timestamping debug messages */
  mesh::RTCClock *_rtc;

  /** Node preferences for configuration settings */
  NodePrefs *_prefs;

  /** Tracks seen packets to prevent loops in broadcast communications */
  SimpleMeshTables _seen_packets;

  /** Packets handed to the mesh inbound queue */
  uint32_t _rx_delivered = 0;

  /** Packets dropped on arrival because this node had already seen them */
  uint32_t _rx_duplicates = 0;

  /** Frames left queued because the mesh packet pool was empty */
  uint32_t _rx_no_packet = 0;

  /** Frames that arrived intact but were not a valid mesh packet */
  uint32_t _rx_unparsed = 0;

  /** Packets not bridged because this node had already sent them */
  uint32_t _tx_duplicates = 0;

  /** Packets too large to frame for this transport */
  uint32_t _tx_oversized = 0;

  /**
   * @brief Constructs a BridgeBase instance
   *
   * @param prefs Node preferences for configuration settings
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  BridgeBase(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
      : _prefs(prefs), _mgr(mgr), _rtc(rtc) {}

  /**
   * @brief Gets formatted date/time string for logging
   *
   * Format: "HH:MM:SS - DD/MM/YYYY U"
   *
   * @return Formatted date/time string
   */
  const char *getLogDateTime();

  /** Zeroes the counters owned by this class. Call from the bridge's resetStats(). */
  void resetBaseStats();

  /**
   * @brief Strip the transport's framing off a received frame.
   *
   * Called by drainRxFrames() once per frame. The default treats the frame as a
   * bare mesh packet blob, which is what a transport that carries no framing of
   * its own (RS232, whose framer has already unwrapped it) needs.
   *
   * @param frame      the frame as popped from the receive queue
   * @param frame_len  length of @p frame
   * @param blob_len   set to the length of the returned blob
   * @return pointer to the mesh packet blob, or nullptr to drop the frame. An
   *         override returning nullptr is expected to count its own reason.
   */
  virtual const uint8_t *unwrapFrame(const uint8_t *frame, size_t frame_len, size_t &blob_len) {
    blob_len = frame_len;
    return frame;
  }

  /**
   * @brief Turn queued frames into mesh packets, bounded per call.
   *
   * Shared by every bridge because the ownership rules are subtle: a pool slot is
   * taken *before* the frame is popped, so an exhausted pool leaves the frame
   * queued for a later loop instead of destroying it, and every path that does
   * not hand the packet to the mesh frees it exactly once.
   *
   * @param queue       frames waiting to be delivered
   * @param max_frames  frames to convert before returning, so one bridge cannot
   *                    monopolise the loop
   * @param scratch     buffer a frame is popped into; must hold the largest frame
   *                    the queue accepts
   * @param scratch_cap capacity of @p scratch
   */
  void drainRxFrames(BridgeFrameQueue &queue, uint8_t max_frames, uint8_t *scratch,
                     size_t scratch_cap);

  /**
   * @brief Common packet handling for received packets
   *
   * Implements the standard pattern used by all bridges:
   * - Check if packet was seen before using _seen_packets.wasSeen()
   * - Queue packet for mesh processing if not seen before
   * - Free packet if already seen to prevent duplicates
   *
   * @param packet The received mesh packet
   */
  void handleReceivedPacket(mesh::Packet *packet);
};
