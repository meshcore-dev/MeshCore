#pragma once

#include "helpers/bridges/BridgeBase.h"

#ifdef WITH_TCP_BRIDGE

#include <WiFiServer.h>
#include <WiFiClient.h>

/**
 * Bridge using the RS232Bridge framing protocol over a WiFi TCP socket.
 *
 * The server listens on the port set by WITH_TCP_BRIDGE. One client is
 * accepted at a time; a new connection displaces the previous one.
 * WiFi must be connected before calling begin().
 *
 * Frame format (identical to RS232Bridge / USBSerialBridge):
 *   [2 bytes] Magic header: 0xC03E
 *   [2 bytes] Payload length
 *   [N bytes] Raw mesh packet bytes
 *   [2 bytes] Fletcher-16 checksum (over payload only)
 *
 * Enable with: -DWITH_TCP_BRIDGE=<port>  (e.g. -DWITH_TCP_BRIDGE=4403)
 */
class TCPBridge : public BridgeBase {
public:
  TCPBridge(NodePrefs* prefs, mesh::PacketManager* mgr, mesh::RTCClock* rtc, uint16_t port);

  void begin() override;
  void end() override;
  void loop() override;
  void sendPacket(mesh::Packet* packet) override;
  void onPacketReceived(mesh::Packet* packet) override;

private:
  static constexpr uint16_t TCP_OVERHEAD = BRIDGE_MAGIC_SIZE + BRIDGE_LENGTH_SIZE + BRIDGE_CHECKSUM_SIZE;
  static constexpr uint16_t MAX_TCP_PACKET_SIZE = (MAX_TRANS_UNIT + 1) + TCP_OVERHEAD;

  uint16_t _port;
  WiFiServer _server;
  WiFiClient _client;
  bool _client_connected = false;
  uint8_t _rx_buffer[MAX_TCP_PACKET_SIZE];
  uint16_t _rx_buffer_pos = 0;
};

#endif
