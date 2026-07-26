#pragma once

#include "MeshCore.h"
#include "esp_now.h"
#include "helpers/bridges/BridgeBase.h"
#include "helpers/bridges/BridgeCodec.h"
#include "helpers/bridges/BridgeFrameQueue.h"
#include "helpers/bridges/BridgeTxQueue.h"

#ifdef WITH_ESPNOW_BRIDGE

/**
 * @brief Bridge implementation using ESP-NOW protocol for packet transport
 *
 * This bridge enables mesh packet transport over ESP-NOW, a connectionless communication
 * protocol provided by Espressif that allows ESP32 devices to communicate directly
 * without WiFi router infrastructure.
 *
 * Features:
 * - Broadcast-based communication (all bridges receive all packets)
 * - Network isolation using XOR encryption with shared secret
 * - Duplicate packet detection using SimpleMeshTables tracking
 * - Maximum packet size of 250 bytes (ESP-NOW limitation)
 *
 * Packet Structure (see BridgeCodec):
 * [2 bytes] Magic Header - Used to identify ESPNowBridge packets
 * [2 bytes] Fletcher-16 checksum of the payload, encrypted
 * [246 bytes max] Encrypted payload containing the mesh packet
 *
 * Threading:
 * ESP-NOW invokes its callbacks on the Wi-Fi task, not the Arduino loop task.
 * The packet pool, inbound queue and seen-packet table have no locking, so the
 * callbacks touch none of them: recv_cb() copies the frame into _rx_frames and
 * send_cb() bumps a counter. All mesh work happens on the main task, in loop()
 * and in sendPacket(), which the mesh calls from logRx()/logTx().
 *
 * Every way a frame can be lost has a counter behind `get bridge.rxstats` and
 * `get bridge.txstats`, so nothing is dropped silently.
 *
 * Configuration:
 * - Define WITH_ESPNOW_BRIDGE to enable this bridge
 * - Define _prefs->bridge_secret with a string to set the network encryption key
 *
 * Network Isolation:
 * Multiple independent mesh networks can coexist by using different
 * _prefs->bridge_secret values. Packets encrypted with a different key will
 * fail the checksum validation and be discarded.
 */
class ESPNowBridge : public BridgeBase, public BridgeFrameSender {
private:
  static ESPNowBridge *_instance;

  // ESP-IDF 5 (Arduino core 3.x, as used by the C6 variants) passes a richer
  // info struct to the receive callback.
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len);
#else
  static void recv_cb(const uint8_t *mac, const uint8_t *data, int32_t len);
#endif
  static void send_cb(const uint8_t *mac, esp_now_send_status_t status);

  /**
   * ESP-NOW Protocol Structure:
   * - ESP-NOW header: 20 bytes (handled by ESP-NOW protocol)
   * - ESP-NOW payload: 250 bytes maximum
   * Total ESP-NOW packet: 270 bytes
   *
   * Our Bridge Packet Structure (must fit in ESP-NOW payload):
   * - Magic header: 2 bytes
   * - Checksum: 2 bytes
   * - Available payload: 246 bytes
   */
  static const size_t MAX_ESPNOW_PACKET_SIZE = 250;

  /** Largest mesh packet blob that still fits an ESP-NOW frame once framed. */
  static const size_t MAX_PAYLOAD_SIZE = MAX_ESPNOW_PACKET_SIZE - BridgeCodec::FRAME_OVERHEAD;

  /** Frames buffered in each direction. */
  static const uint8_t RX_QUEUE_DEPTH = 8;
  static const uint8_t TX_QUEUE_DEPTH = 8;

  /** Received frames turned into mesh packets per loop() call. */
  static const uint8_t RX_DRAIN_PER_LOOP = 4;

  /** Transmissions tried per frame before it is abandoned. */
  static const uint8_t TX_MAX_ATTEMPTS = 3;

  /** How long to wait for the ESP-NOW send callback before assuming it is lost. */
  static const uint32_t TX_ACK_TIMEOUT_MS = 250;

  /** Pause between transmission attempts. */
  static const uint32_t TX_RETRY_DELAY_MS = 20;

  /** Frames handed over by the Wi-Fi task, drained by loop() on the main task. */
  BridgeFrameQueue _rx_frames;

  /** Outbound frames, paced one at a time and retried when the radio refuses. */
  BridgeTxQueue _tx_frames;

  /**
   * Decrypted payload handed to the mesh. A member rather than a local so that
   * unwrapFrame() can return it; loop() holds the encrypted frame at the same
   * time, so this costs no more than the two stack buffers it replaces.
   */
  uint8_t _rx_blob[MAX_PAYLOAD_SIZE];

  uint32_t _rx_invalid;  ///< failed magic or checksum: foreign network, or corrupt

  /** Decrypts a received frame; see BridgeBase::unwrapFrame(). */
  const uint8_t *unwrapFrame(const uint8_t *frame, size_t frame_len, size_t &blob_len) override;

public:
  /**
   * Constructs an ESPNowBridge instance
   *
   * @param prefs Node preferences for configuration settings
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  ESPNowBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc);

  /**
   * Initializes the ESP-NOW bridge
   *
   * - Configures WiFi in station mode with power save disabled
   * - Initializes ESP-NOW protocol
   * - Registers callbacks
   * - Sets up broadcast peer
   */
  void begin() override;

  /**
   * Stops the ESP-NOW bridge
   *
   * - Removes broadcast peer
   * - Unregisters callbacks
   * - Deinitializes ESP-NOW protocol
   * - Turns off WiFi to release radio resources
   */
  void end() override;

  /**
   * Main loop handler
   *
   * Drains received frames into the mesh and drives the transmit queue. Must be
   * called regularly; nothing else moves packets through the bridge.
   */
  void loop() override;

  /**
   * Called when a packet is received via ESP-NOW
   * Queues the packet for mesh processing if not seen before
   *
   * @param packet The received mesh packet
   */
  void onPacketReceived(mesh::Packet *packet) override;

  /**
   * Called when a packet needs to be transmitted via ESP-NOW
   * Frames and queues the packet if not seen before
   *
   * @param packet The mesh packet to transmit
   */
  void sendPacket(mesh::Packet *packet) override;

  /** Hands one frame to ESP-NOW. Called by the transmit queue. */
  bool sendFrame(const uint8_t *frame, size_t len) override;

  /**
   * @brief Writes the receive-side counters into @p dest.
   *
   * Comparing TX on one node against RX on another shows where frames go missing.
   */
  void getRxStats(char *dest, size_t dest_size) const;

  /** @brief Writes the transmit-side counters into @p dest. */
  void getTxStats(char *dest, size_t dest_size) const;

  /** Zeroes every counter, so a measurement can start from a known point. */
  void resetStats();
};

#endif
