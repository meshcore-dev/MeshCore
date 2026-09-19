#pragma once

#include "MeshCore.h"
#include "esp_now.h"
#include "helpers/bridges/BridgeBase.h"

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
 * Packet Structure:
 * [2 bytes] Magic Header - Used to identify ESPNowBridge packets
 * [2 bytes] Fletcher-16 checksum of encrypted payload (calculated over payload only)
 * [246 bytes max] Encrypted payload containing the mesh packet
 *
 * The Fletcher-16 checksum is used to validate packet integrity and detect
 * corrupted or tampered packets. It's calculated over the encrypted payload
 * and provides a simple but effective way to verify packets are both
 * uncorrupted and from the same network (since the checksum is calculated
 * after encryption).
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
class ESPNowBridge : public BridgeBase {
private:
  static ESPNowBridge *_instance;
  static void recv_cb(const uint8_t *mac, const uint8_t *data, int32_t len);
  static void send_cb(const uint8_t *mac, esp_now_send_status_t status);

  /**
   * Control frame magic, deliberately different from BRIDGE_PACKET_MAGIC.
   *
   * A presence announcement says "this node has a bridge, on this medium, right
   * now" - it is what makes "peer known ESP-NOW-reachable" an observation rather
   * than a guess, because no mesh packet type carries a source hash for every
   * kind of traffic (an ACK, for instance, carries only its CRC). Other
   * implementations of this bridge see a frame with an unknown magic and discard
   * it, so the lane stays interoperable with the upstream ESP-NOW bridges.
   */
  static constexpr uint16_t BRIDGE_ANNOUNCE_MAGIC = 0xC03F;

  /** How often a running bridge announces itself, and how long a peer is trusted. */
  static constexpr uint32_t BRIDGE_ANNOUNCE_INTERVAL_MS = 5000;
  static constexpr uint32_t BRIDGE_PEER_TTL_MS = 30000;

  /** Bridge peers remembered for transport selection. */
  static constexpr size_t MAX_BRIDGE_PEERS = 8;

  /** Injected packets whose round trip through the mesh is still being measured. */
  static constexpr size_t INFLIGHT_SLOTS = 8;

  struct PeerEntry {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    uint8_t hash;            // mesh hash (first byte of the peer's public key)
    uint32_t last_seen;      // millis() of the last frame from this peer
    bool used;
  };

  struct InflightEntry {
    uint8_t hash[MAX_HASH_SIZE];
    uint32_t received;       // millis() when the frame arrived over ESP-NOW
    bool used;
  };

  /**
   * Counters for the host's bridge-stats frame. Every entry answers a question the
   * last measurement could only infer: did the packet really skip the radio, did
   * the packet arrive over this lane, and how much of the latency is the bridge's.
   */
  struct Counters {
    uint32_t tx;                 // data frames handed to esp_now_send()
    uint32_t tx_announce;        // presence announcements handed to esp_now_send()
    uint32_t tx_failed;          // esp_now_send() refused the frame outright
    uint32_t tx_done;            // send-callback: frame left the radio
    uint32_t tx_error;           // send-callback: ESP-NOW reported a failure
    uint32_t rx;                 // valid bridge frames received
    uint32_t rx_announce;        // presence announcements received
    uint32_t rx_bad;             // frames dropped: magic, size or checksum
    uint32_t dropped_nospace;    // no free packet in the pool
    uint32_t fastlane_tx;        // sent on ESP-NOW only: the radio was skipped
    uint32_t mirror_tx;          // mirrored before the radio transmit
    uint32_t fastlane_miss;      // unicast that could NOT use the lane (peer unknown)
    uint32_t rx_to_queue_max_ms; // bridge's own receive-path cost, worst case
    uint32_t rx_to_queue_last_ms;
    uint32_t inject_to_process_max_ms;   // arrival -> processed by the mesh
    uint32_t inject_to_process_last_ms;
    uint32_t inject_delay_last_ms;       // hold the last injected packet got
  };

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

  /**
   * Size constants for packet parsing
   */
  static const size_t MAX_PAYLOAD_SIZE = MAX_ESPNOW_PACKET_SIZE - (BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE);

  /** Buffer for receiving ESP-NOW packets */
  uint8_t _rx_buffer[MAX_ESPNOW_PACKET_SIZE];

  /** Current position in receive buffer */
  size_t _rx_buffer_pos;

  /** This node's own mesh hash, so an announcement can carry it. */
  uint8_t _self_hash;
  bool _have_self_hash;
  uint32_t _last_announce;

  /** Peers heard on this lane recently enough to send to without the radio. */
  PeerEntry _peers[MAX_BRIDGE_PEERS];

  /** Bridge-received packets whose processing time is still being measured. */
  InflightEntry _inflight[INFLIGHT_SLOTS];
  uint8_t _inflight_next;

  Counters _counters;

  /**
   * Records a peer as reachable over this lane, refreshing it if already known.
   */
  void notePeer(const uint8_t *mac, uint8_t hash, uint32_t now);

  /**
   * Learns the sender of a mesh packet that crossed this lane in one hop.
   *
   * A directly routed data packet with no hops came from the node whose hash it
   * carries as its source, so it is evidence - independent of the announcements -
   * that this peer is on the lane right now.
   */
  void notePeerFromPacket(const uint8_t *mac, const mesh::Packet *packet, uint32_t now);

  /**
   * Handles a peer's presence announcement.
   */
  void handleAnnounce(const uint8_t *mac, const uint8_t *data, int32_t len, uint32_t now);

  /**
   * @returns true if the mesh hash was heard on this lane within the TTL.
   */
  bool peerReachable(uint8_t hash, uint32_t now);

  /**
   * Sends this node's presence announcement, so peers can select this lane.
   */
  void sendAnnounce(uint32_t now);

  /**
   * Builds and sends one bridge frame: magic header, checksum, encrypted payload.
   * Shared by packets and announcements, which differ only in magic and payload.
   */
  void sendFrame(uint16_t magic, const uint8_t *payload, size_t payload_len, bool is_announce);

  /**
   * Remembers an injected packet so its processing time can be measured, and
   * closes the measurement when the mesh hands the packet to its handlers.
   */
  void recordInflight(const mesh::Packet *packet, uint32_t now);
  void completeInflight(const mesh::Packet *packet, uint32_t now);

  /**
   * Performs XOR encryption/decryption of data
   * Used to isolate different mesh networks
   *
   * Uses _prefs->bridge_secret as the key in a simple XOR operation.
   * The same operation is used for both encryption and decryption.
   * While not cryptographically secure, it provides basic network isolation.
   *
   * @param data Pointer to data to encrypt/decrypt
   * @param len Length of data in bytes
   */
  void xorCrypt(uint8_t *data, size_t len);

  /**
   * ESP-NOW receive callback
   * Called by ESP-NOW when a packet is received
   *
   * @param mac Source MAC address
   * @param data Received data
   * @param len Length of received data
   */
  void onDataRecv(const uint8_t *mac, const uint8_t *data, int32_t len);

  /**
   * ESP-NOW send callback
   * Called by ESP-NOW after a transmission attempt
   *
   * @param mac_addr Destination MAC address
   * @param status Transmission status
   */
  void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

public:
  /**
   * Constructs an ESPNowBridge instance
   *
   * @param prefs Node preferences for configuration settings
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  ESPNowBridge(BridgePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc);

  /**
   * Initializes the ESP-NOW bridge
   *
   * - Configures WiFi in station mode
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
   * ESP-NOW is callback-based, so this is currently empty
   */
  void loop() override;

  /**
   * Sets this node's mesh hash, so peers can be told this lane exists.
   * Called by the owning mesh once its identity is known.
   */
  void setSelfHash(uint8_t hash) { _self_hash = hash; _have_self_hash = true; }

  /**
   * Offers an outbound packet to the local lane, before the radio transmits it.
   *
   * A packet the mesh already routes directly to a peer that has announced
   * itself on this lane needs no radio airtime: it travels over ESP-NOW only, and
   * the radio's counters stay flat. Everything else keeps the radio as the
   * transport of record and is mirrored instead, so a nearby peer still gets a
   * copy before the airtime is paid.
   *
   * @param packet The packet the dispatcher is about to transmit.
   * @returns true if the packet went out over ESP-NOW and the radio must skip it.
   */
  bool claimOutboundPacket(mesh::Packet *packet) override;

  /**
   * Called when a packet this bridge injected is processed by the mesh.
   */
  void onInboundPacketProcessed(mesh::Packet *packet) override;

  /**
   * Writes the bridge counters for the host's bridge-stats frame.
   */
  size_t writeStats(uint8_t *dest, size_t max_len) override;

  /**
   * A unicast packet that arrives on this lane and is not forwarded (a direct
   * route with no hops) is not held: no radio copy is coming, and the hold is
   * pure latency in the fast path. Broadcast/flood traffic keeps the configured
   * delay, so the radio's copy still wins the race and no extra reaction follows.
   */
  uint32_t getInjectDelayMs(const mesh::Packet *packet) override;

  /**
   * Called when a packet is received via ESP-NOW
   * Queues the packet for mesh processing if not seen before
   *
   * @param packet The received mesh packet
   */
  void onPacketReceived(mesh::Packet *packet) override;

  /**
   * Called when a packet needs to be transmitted via ESP-NOW
   * Encrypts and broadcasts the packet if not seen before
   *
   * @param packet The mesh packet to transmit
   */
  void sendPacket(mesh::Packet *packet) override;
};

#endif
