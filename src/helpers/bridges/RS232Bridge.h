#pragma once

#include "helpers/bridges/BridgeBase.h"

#include <Stream.h>

#ifdef WITH_RS232_BRIDGE

/**
 * @brief Bridge implementation using RS232/UART protocol for packet transport
 *
 * This bridge enables mesh packet transport over serial/UART connections,
 * allowing nodes to communicate over wired serial links. It implements a simple
 * packet framing protocol with checksums for reliable transfer.
 *
 * Features:
 * - Point-to-point communication over hardware UART
 * - Fletcher-16 checksum for data integrity verification
 * - Magic header for packet synchronization and frame alignment
 * - Duplicate packet detection using SimpleMeshTables tracking
 * - Configurable RX/TX pins via build defines
 * - Fixed baud rate at 115200 for consistent timing
 *
 * Packet Structure:
 * [2 bytes] Magic Header (0xC03E) - Used to identify start of RS232Bridge packets
 * [2 bytes] Payload Length - Length of the mesh packet payload
 * [n bytes] Mesh Packet Payload - The actual mesh packet data
 * [2 bytes] Fletcher-16 Checksum - Calculated over the payload for integrity verification
 *
 * The Fletcher-16 checksum is calculated over the mesh packet payload and provides
 * error detection capabilities suitable for serial communication where electrical
 * noise, timing issues, or hardware problems could corrupt data. The checksum
 * validation ensures only valid packets are forwarded to the mesh.
 *
 * Configuration:
 * - Define WITH_RS232_BRIDGE to enable this bridge
 * - Define WITH_RS232_BRIDGE_RX with the RX pin number
 * - Define WITH_RS232_BRIDGE_TX with the TX pin number
 *
 * Platform Support:
 * Different platforms require different pin configuration methods:
 * - ESP32: Uses HardwareSerial::setPins(rx, tx)
 * - NRF52: Uses Uart::setPins(rx, tx)
 * - RP2040: Uses SerialUART::setRX(rx) and SerialUART::setTX(tx)
 * - STM32: Uses HardwareSerial::setRx(rx) and HardwareSerial::setTx(tx)
 */
class RS232Bridge : public BridgeBase {
public:
  /**
   * @brief Constructs an RS232Bridge instance
   *
   * @param prefs Node preferences for configuration settings
   * @param serial The hardware serial port to use
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  RS232Bridge(NodePrefs *prefs, Stream &serial, mesh::PacketManager *mgr, mesh::RTCClock *rtc);

  /**
   * Initializes the RS232 bridge
   *
   * - Validates that RX/TX pins are defined
   * - Configures UART pins based on target platform
   * - Sets baud rate to 115200 for consistent communication
   * - Platform-specific pin configuration methods are used
   */
  void begin() override;

  /**
   * Stops the RS232 bridge
   *
   */
  void end() override;

  /**
   * @brief Main loop handler for processing incoming serial data
   *
   * Implements a state machine for packet reception:
   * 1. Searches for magic header bytes for packet synchronization
   * 2. Reads length field to determine expected packet size
   * 3. Validates packet length against maximum allowed size
   * 4. Receives complete packet payload and checksum
   * 5. Validates Fletcher-16 checksum for data integrity
   * 6. Creates mesh packet and forwards if valid
   */
  void loop() override;

  /**
   * @brief Called when a packet needs to be transmitted over serial
   *
   * Formats the mesh packet with RS232 framing protocol:
   * - Adds magic header for synchronization
   * - Includes payload length field
   * - Calculates Fletcher-16 checksum over payload
   * - Transmits complete framed packet
   * - Uses duplicate detection to prevent retransmission
   *
   * @param packet The mesh packet to transmit
   */
  void sendPacket(mesh::Packet *packet) override;

  /**
   * @brief Called when a complete valid packet has been received from serial
   *
   * Forwards the received packet to the mesh for processing.
   * The packet has already been validated for checksum integrity
   * and parsed successfully at this point.
   *
   * @param packet The received mesh packet ready for processing
   */
  void onPacketReceived(mesh::Packet *packet) override;

private:
  /**
   * RS232 Protocol Structure:
   * - Magic header: 2 bytes (packet identification)
   * - Length field: 2 bytes (payload length)
   * - Payload: variable bytes (mesh packet data)
   * - Checksum: 2 bytes (Fletcher-16 over payload)
   * Total overhead: 6 bytes
   */

  /**
   * @brief The total overhead of the serial protocol in bytes.
   * Includes: MAGIC_WORD (2) + LENGTH (2) + CHECKSUM (2) = 6 bytes
   */
  static constexpr uint16_t SERIAL_OVERHEAD = BRIDGE_MAGIC_SIZE + BRIDGE_LENGTH_SIZE + BRIDGE_CHECKSUM_SIZE;

  /**
   * @brief The maximum size of a complete packet on the serial line.
   *
   * This is calculated as the sum of:
   * - MAX_TRANS_UNIT + 1 for the maximum mesh packet size
   * - SERIAL_OVERHEAD for the framing (magic + length + checksum)
   */
  static constexpr uint16_t MAX_SERIAL_PACKET_SIZE = (MAX_TRANS_UNIT + 1) + SERIAL_OVERHEAD;

  /** Hardware serial port interface */
  Stream *_serial;

  /** Buffer for building received packets */
  uint8_t _rx_buffer[MAX_SERIAL_PACKET_SIZE];

  /** Current position in the receive buffer */
  uint16_t _rx_buffer_pos = 0;
};

#endif
