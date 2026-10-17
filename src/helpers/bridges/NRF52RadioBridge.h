#pragma once

#include "MeshCore.h"
#include "helpers/bridges/BridgeBase.h"

#ifdef WITH_NRF52_WIRELESS_BRIDGE

#ifndef NRF52_WIRELESS_BRIDGE_LBT_RSSI
#define NRF52_WIRELESS_BRIDGE_LBT_RSSI -70     // dBm, channel considered busy above this level
#endif

/**
 * @brief Bridge implementation using the nRF52 2.4GHz radio for packet transport
 *
 * ESP-NOW rides on 802.11 (DSSS/OFDM) frames, which the nRF52 RADIO peripheral cannot
 * modulate or demodulate (it only supports GFSK Nordic proprietary/BLE modes and, on
 * nRF52840, 802.15.4 O-QPSK). This bridge therefore implements an ESP-NOW-like protocol
 * of its own on top of 1Mbit GFSK (BLE 1M PHY modulation, custom framing), and is only interoperable
 * with other nRF52 nodes running this bridge - not with ESP-NOW bridges.
 *
 * It mimics ESPNowBridge behaviour so it is a drop-in replacement:
 * - Connectionless broadcast; every bridge on the same channel receives every packet
 * - bridge.channel selects one of NUM_CHANNELS frequencies placed in the gaps between Wi-Fi
 *   channels 1/6/11 and BLE advertising channels, as GFSK is narrow enough to fit there:
 *   1 = 2482 MHz (default), 2 = 2450 MHz, 3 = 2424 MHz
 * - Network isolation using XOR encryption with _prefs->bridge_secret + Fletcher-16
 * - Same payload layout as ESPNowBridge
 * - Listen-before-talk with random backoff instead of 802.11 CSMA/CA
 *
 * The RADIO peripheral is driven directly, so the SoftDevice must NOT be enabled while the
 * bridge runs (true for repeaters, where BLE is only started for OTA). begin() refuses to
 * start if the SoftDevice is enabled, so this bridge cannot be combined with BLE companion.
 *
 * Air frame (1Mbit GFSK, whitened):
 * [1 byte]  preamble
 * [5 bytes] address (fixed network address)
 * [1 byte]  length
 * [2 bytes] Magic Header - Used to identify bridge packets
 * [2 bytes] Fletcher-16 checksum of payload (XOR encrypted)
 * [251 bytes max] Mesh packet (XOR encrypted)
 * [2 bytes] CRC-16/CCITT (hardware)
 */
class NRF52RadioBridge : public BridgeBase {
private:
  static NRF52RadioBridge *_instance;

  /** Radio payload limit (8-bit length field) */
  static const size_t MAX_RADIO_PACKET_SIZE = 255;

  /** Space left for the mesh packet after magic + checksum */
  static const size_t MAX_PAYLOAD_SIZE = MAX_RADIO_PACKET_SIZE - (BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE);

  /** RX/TX ring sizes (one RX slot is always owned by the radio) */
  static const uint8_t RX_SLOTS = 4;
  static const uint8_t TX_SLOTS = 4;

  /** Max times a TX is deferred due to a busy channel before sending anyway */
  static const uint8_t MAX_LBT_ATTEMPTS = 10;

  /** Safety timeout for a TX that never signals completion */
  static const uint32_t TX_TIMEOUT_MS = 20;

  /** Radio buffer: [0] = length, [1..] = payload, as the RADIO DMA expects */
  struct RadioFrame {
    uint8_t data[1 + MAX_RADIO_PACKET_SIZE];
    int8_t rssi;
  };

  enum RadioState : uint8_t { STATE_OFF, STATE_RX, STATE_TX };

  RadioFrame _rx_frames[RX_SLOTS];
  volatile uint8_t _rx_head; // slot the radio is receiving into (ISR owned)
  volatile uint8_t _rx_tail; // next slot to process (loop owned)

  RadioFrame _tx_frames[TX_SLOTS];
  uint8_t _tx_head;
  uint8_t _tx_tail;
  bool _tx_in_flight;
  uint32_t _tx_started_at;
  uint32_t _tx_next_attempt;
  uint8_t _tx_attempts;

  volatile RadioState _state;
  volatile bool _rx_busy;  // address matched, packet reception in progress
  volatile bool _tx_done;  // set by ISR when TX finished

  bool _hfxo_started;      // we started HFXO and must stop it in end()

#if BRIDGE_DEBUG
  volatile uint32_t _dbg_addr, _dbg_crc_ok, _dbg_crc_err;
  uint32_t _dbg_next_report;
#endif

  void onRadioIrq();

  /** Maps bridge channel 1-NUM_CHANNELS to NRF_RADIO->FREQUENCY (MHz offset from 2400) */
  static uint8_t channelToFrequency(uint8_t channel);

  void configureRadio();
  void startRx();
  bool isChannelClear();
  void transmitFrame(RadioFrame *frame);
  void serviceTx();
  void processRxFrame(const RadioFrame *frame);

public:
  /** Number of selectable bridge.channel values */
  static const uint8_t NUM_CHANNELS = 3;

  /** bridge.txpower range in dBm; values in between are rounded down to a supported TXPOWER step */
  static const int8_t MIN_TX_POWER = -20;
#if defined(RADIO_TXPOWER_TXPOWER_Pos8dBm)
  static const int8_t MAX_TX_POWER = 8;
#else
  static const int8_t MAX_TX_POWER = 4;
#endif

  /** Maps dBm to the highest supported TXPOWER step that does not exceed it, returns that step in dBm */
  static int8_t supportedTxPower(int8_t dbm);

  /** Entry point for RADIO_IRQHandler, dispatches to the active instance */
  static void handleIrq();

  /**
   * Constructs an NRF52RadioBridge instance
   *
   * @param prefs Node preferences for configuration settings
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  NRF52RadioBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc);

  /**
   * Initializes the bridge
   *
   * - Starts the high frequency crystal oscillator (required by RADIO)
   * - Configures the RADIO for 1Mbit GFSK on _prefs->bridge_channel
   * - Starts continuous receive
   */
  void begin() override;

  /**
   * Stops the bridge
   *
   * - Disables and resets the RADIO peripheral
   * - Stops the HFXO if it was started by begin()
   */
  void end() override;

  /**
   * Main loop handler
   * Processes received frames and transmits queued frames (with listen-before-talk)
   */
  void loop() override;

  /**
   * Called when a packet is received via the radio
   * Queues the packet for mesh processing if not seen before
   *
   * @param packet The received mesh packet
   */
  void onPacketReceived(mesh::Packet *packet) override;

  /**
   * Called when a packet needs to be transmitted via the radio
   * Encrypts and queues the packet for broadcast if not seen before
   *
   * @param packet The mesh packet to transmit
   */
  void sendPacket(mesh::Packet *packet) override;
};

#endif
