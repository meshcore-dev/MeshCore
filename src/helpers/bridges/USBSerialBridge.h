#pragma once

#include "helpers/bridges/BridgeBase.h"

#include <Stream.h>

#ifdef WITH_USB_SERIAL_BRIDGE

/**
 * Bridge using the RS232Bridge framing protocol over a USB CDC serial stream.
 *
 * Unlike RS232Bridge, this class takes any Stream (e.g. Serial on ESP32-S3 native USB)
 * and skips all HardwareSerial pin/baud configuration — the USB CDC port is already
 * initialised by the framework before setup() runs.
 *
 * Frame format (identical to RS232Bridge):
 *   [2 bytes] Magic header: 0xC03E
 *   [2 bytes] Payload length
 *   [N bytes] Raw mesh packet bytes
 *   [2 bytes] Fletcher-16 checksum (over payload only)
 *
 * Enable with: -DWITH_USB_SERIAL_BRIDGE=Serial  (or USBSerial, etc.)
 */
class USBSerialBridge : public BridgeBase {
public:
  USBSerialBridge(NodePrefs* prefs, Stream& serial, mesh::PacketManager* mgr, mesh::RTCClock* rtc);

  void begin() override;
  void end() override;
  void loop() override;
  void sendPacket(mesh::Packet* packet) override;
  void onPacketReceived(mesh::Packet* packet) override;

private:
  static constexpr uint16_t SERIAL_OVERHEAD = BRIDGE_MAGIC_SIZE + BRIDGE_LENGTH_SIZE + BRIDGE_CHECKSUM_SIZE;
  static constexpr uint16_t MAX_SERIAL_PACKET_SIZE = (MAX_TRANS_UNIT + 1) + SERIAL_OVERHEAD;

  Stream* _serial;
  uint8_t _rx_buffer[MAX_SERIAL_PACKET_SIZE];
  uint16_t _rx_buffer_pos = 0;
};

#endif
