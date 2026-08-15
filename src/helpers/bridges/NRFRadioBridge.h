#pragma once

#include "MeshCore.h"
#include "helpers/bridges/BridgeBase.h"
#include "helpers/bridges/NRFRadioChannel.h"

#ifdef WITH_NRF_BRIDGE

extern "C" void RADIO_IRQHandler(void);

/**
 * @brief Bridge implementation using the nRF52 2.4GHz proprietary radio
 *
 * Carries mesh packets over Nordic's 1 Mbit proprietary mode, doing what
 * ESPNowBridge does on ESP32: one fixed address that every bridge shares, so a
 * transmission reaches all of them, with no pairing or acknowledgement.
 *
 * The two radios are not compatible on air. An nRF bridge only pairs with
 * another nRF bridge.
 *
 * Features:
 * - Broadcast-based communication (all bridges receive all frames)
 * - Network isolation using XOR encryption with shared secret
 * - Duplicate packet detection using SimpleMeshTables tracking
 * - Maximum frame size of 255 bytes (RADIO LENGTH field is 8 bits)
 *
 * Packet Structure (identical to ESPNowBridge's):
 * [2 bytes] Magic Header - Used to identify bridge frames
 * [2 bytes] Fletcher-16 checksum of the payload, encrypted
 * [251 bytes max] Encrypted payload containing the mesh packet
 *
 * RADIO checks a 16-bit CRC in hardware, so a corrupted frame never reaches the
 * bridge. The Fletcher-16 is left doing one job: rejecting frames from a network
 * using a different secret.
 *
 * Interrupt context:
 * RADIO reports frames in an interrupt rather than on a driver task. The packet
 * pool, inbound queue and seen-packet table have no locking, so the handler
 * touches none of them: it copies the frame into _rx_queue, and loop() turns
 * those into mesh packets on the main task. This is the one structural
 * difference from ESPNowBridge, which can do that work in its callback.
 *
 * Configuration:
 * - Define WITH_NRF_BRIDGE to enable this bridge
 * - Set _prefs->bridge_secret to the network encryption key
 * - Set _prefs->bridge_channel; the Wi-Fi 1-14 numbering is shared with the
 *   ESP-NOW bridge and maps onto the same frequencies (see NRFRadioChannel)
 *
 * SoftDevice:
 * The nRF52 has one RADIO and the SoftDevice owns it whenever BLE is enabled, so
 * this bridge refuses to start while the SoftDevice is up and must be stopped
 * before anything enables it (see NRF52Board::startOTAUpdate). That is why it
 * ships on repeater and room server builds only, matching ESP-NOW.
 */
class NRFRadioBridge : public BridgeBase {
  // The RADIO vector is a free function, so it needs reaching in here to dispatch
  friend void ::RADIO_IRQHandler(void);

private:
  static NRFRadioBridge *_instance;

  /** RADIO interrupt handler, dispatched to the instance. */
  static void radio_isr();

  /**
   * RADIO frame structure:
   * - Preamble and address: sent by the hardware, not counted here
   * - LENGTH byte: 8 bits, so a frame carries up to 255 bytes
   * - CRC: 2 bytes appended and checked by the hardware
   *
   * Our Bridge Packet Structure (must fit in the RADIO payload):
   * - Magic header: 2 bytes
   * - Checksum: 2 bytes
   * - Available payload: 251 bytes
   */
  static const size_t MAX_NRF_PACKET_SIZE = 255;

  /** Largest mesh packet blob that still fits a RADIO frame once framed. */
  static const size_t MAX_PAYLOAD_SIZE =
      MAX_NRF_PACKET_SIZE - (BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE);

  /**
   * On-air address, shared by every bridge regardless of secret.
   *
   * Fixed rather than derived from bridge_secret, so a node holding the wrong
   * secret still hears its neighbours and drops their frames on the checksum,
   * the way ESP-NOW does. Filtering in hardware would leave a bad secret looking
   * identical to a dead radio.
   */
  static const uint32_t ADDRESS_BASE = 0xC03E5BE1;
  static const uint8_t ADDRESS_PREFIX = 0xC0;

  /** Frames the interrupt can buffer before loop() gets to them. */
  static const uint8_t RX_QUEUE_DEPTH = 4;

  /** Frames turned into mesh packets per loop() call. */
  static const uint8_t RX_DRAIN_PER_LOOP = 4;

  /** What the radio is doing. Written by the interrupt and by begin()/end(). */
  enum RadioState : uint8_t {
    RADIO_OFF,  ///< peripheral disabled, bridge stopped
    RADIO_RX,   ///< listening, or about to be re-armed
    RADIO_TX,   ///< transmitting a frame
  };

  volatile RadioState _radio_state;

  /**
   * RADIO reads and writes the frame through PACKETPTR, LENGTH byte first. One
   * buffer per direction, so transmitting cannot clobber a frame arriving.
   */
  uint8_t _rx_raw[1 + MAX_NRF_PACKET_SIZE];
  uint8_t _tx_raw[1 + MAX_NRF_PACKET_SIZE];

  /**
   * Frames waiting to become mesh packets. Single producer (the interrupt,
   * advancing _rx_head) and single consumer (loop(), advancing _rx_tail), so
   * neither needs to lock the other out.
   */
  uint8_t _rx_queue[RX_QUEUE_DEPTH][MAX_NRF_PACKET_SIZE];
  uint8_t _rx_queue_len[RX_QUEUE_DEPTH];
  volatile uint8_t _rx_head;
  volatile uint8_t _rx_tail;

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
   * @brief Makes sure the radio has the external crystal, starting it if not.
   *
   * @return true if HFCLK is running from the crystal
   */
  static bool startHighFreqClock();

  /**
   * @brief Applies the RADIO configuration. Called once by begin().
   *
   * @param frequency MHz above 2400, as returned by NRFRadioChannel
   */
  void configureRadio(uint8_t frequency);

  /** Points RADIO at the receive buffer and starts listening. */
  void armReceive();

  /** Brings RADIO to a stop, waiting for it to report DISABLED. */
  void disableRadio();

  /** Handles one RADIO interrupt. */
  void onRadioEvent();

  /** Turns one queued frame into a mesh packet. */
  void handleFrame(const uint8_t *frame, size_t len);

  /** Hands a framed blob to the radio. @return false if it could not be sent. */
  bool sendFrame(const uint8_t *frame, size_t len);

public:
  /**
   * Constructs an NRFRadioBridge instance
   *
   * @param prefs Node preferences for configuration settings
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  NRFRadioBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc);

  /**
   * Initializes the nRF proprietary radio bridge
   *
   * - Refuses to start if the SoftDevice owns the radio
   * - Refuses to start on a channel outside 1-14, as ESP-NOW does
   * - Configures RADIO for 1 Mbit Nordic mode on the configured channel
   * - Enables the RADIO interrupt and starts listening
   */
  void begin() override;

  /**
   * Stops the nRF proprietary radio bridge
   *
   * - Disables the RADIO interrupt
   * - Brings the peripheral to a stop, releasing it for BLE
   * - Drops anything still buffered
   */
  void end() override;

  /**
   * Main loop handler
   *
   * Turns frames buffered by the interrupt into mesh packets. Must be called
   * regularly; nothing else moves received packets through the bridge.
   */
  void loop() override;

  /**
   * Called when a packet is received over the air
   * Queues the packet for mesh processing if not seen before
   *
   * @param packet The received mesh packet
   */
  void onPacketReceived(mesh::Packet *packet) override;

  /**
   * Called when a packet needs to be transmitted
   * Encrypts and broadcasts the packet if not seen before
   *
   * @param packet The mesh packet to transmit
   */
  void sendPacket(mesh::Packet *packet) override;
};

#endif
