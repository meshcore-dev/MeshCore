#include "TCPBridge.h"

#ifdef WITH_TCP_BRIDGE

TCPBridge::TCPBridge(NodePrefs* prefs, mesh::PacketManager* mgr, mesh::RTCClock* rtc, uint16_t port)
    : BridgeBase(prefs, mgr, rtc), _port(port), _server(port) {}

void TCPBridge::begin() {
  _server.begin();
  _initialized = true;
  BRIDGE_DEBUG_PRINTLN("TCP bridge listening on port %d\n", _port);
}

void TCPBridge::end() {
  if (_client_connected) {
    _client.stop();
    _client_connected = false;
  }
  _server.end();
  _initialized = false;
  BRIDGE_DEBUG_PRINTLN("TCP bridge stopped\n");
}

void TCPBridge::loop() {
  if (!_initialized) return;

  // Accept new connection, displacing any existing one
  WiFiClient incoming = _server.available();
  if (incoming) {
    if (_client_connected) {
      _client.stop();
    }
    _client = incoming;
    _client_connected = true;
    _rx_buffer_pos = 0;
    BRIDGE_DEBUG_PRINTLN("TCP client connected\n");
  }

  if (_client_connected) {
    if (!_client.connected()) {
      _client.stop();
      _client_connected = false;
      BRIDGE_DEBUG_PRINTLN("TCP client disconnected\n");
      return;
    }

    while (_client.available()) {
      uint8_t b = _client.read();

      if (_rx_buffer_pos < 2) {
        if ((_rx_buffer_pos == 0 && b == ((BRIDGE_PACKET_MAGIC >> 8) & 0xFF)) ||
            (_rx_buffer_pos == 1 && b == (BRIDGE_PACKET_MAGIC & 0xFF))) {
          _rx_buffer[_rx_buffer_pos++] = b;
        } else {
          _rx_buffer_pos = 0;
          if (b == ((BRIDGE_PACKET_MAGIC >> 8) & 0xFF)) {
            _rx_buffer[_rx_buffer_pos++] = b;
          }
        }
      } else {
        _rx_buffer[_rx_buffer_pos++] = b;

        if (_rx_buffer_pos >= 4) {
          uint16_t len = (_rx_buffer[2] << 8) | _rx_buffer[3];

          if (len > (MAX_TRANS_UNIT + 1)) {
            BRIDGE_DEBUG_PRINTLN("RX invalid length %d, resetting\n", len);
            _rx_buffer_pos = 0;
            continue;
          }

          if (_rx_buffer_pos == len + TCP_OVERHEAD) {
            uint16_t received_checksum = (_rx_buffer[4 + len] << 8) | _rx_buffer[5 + len];

            if (validateChecksum(_rx_buffer + 4, len, received_checksum)) {
              BRIDGE_DEBUG_PRINTLN("RX, len=%d crc=0x%04x\n", len, received_checksum);
              mesh::Packet* pkt = _mgr->allocNew();
              if (pkt) {
                if (pkt->readFrom(_rx_buffer + 4, len)) {
                  onPacketReceived(pkt);
                } else {
                  BRIDGE_DEBUG_PRINTLN("RX failed to parse packet\n");
                  _mgr->free(pkt);
                }
              } else {
                BRIDGE_DEBUG_PRINTLN("RX failed to allocate packet\n");
              }
            } else {
              BRIDGE_DEBUG_PRINTLN("RX checksum mismatch, rcv=0x%04x\n", received_checksum);
            }
            _rx_buffer_pos = 0;
          }
        }
      }
    }
  }
}

void TCPBridge::sendPacket(mesh::Packet* packet) {
  if (!_initialized || !packet || !_client_connected) return;

  if (!_seen_packets.hasSeen(packet)) {
    uint8_t buffer[MAX_TCP_PACKET_SIZE];
    uint16_t len = packet->writeTo(buffer + 4);

    if (len > (MAX_TRANS_UNIT + 1)) {
      BRIDGE_DEBUG_PRINTLN("TX packet too large (payload=%d, max=%d)\n", len, MAX_TRANS_UNIT + 1);
      return;
    }

    buffer[0] = (BRIDGE_PACKET_MAGIC >> 8) & 0xFF;
    buffer[1] = BRIDGE_PACKET_MAGIC & 0xFF;
    buffer[2] = (len >> 8) & 0xFF;
    buffer[3] = len & 0xFF;

    uint16_t checksum = fletcher16(buffer + 4, len);
    buffer[4 + len] = (checksum >> 8) & 0xFF;
    buffer[5 + len] = checksum & 0xFF;

    _client.write(buffer, len + TCP_OVERHEAD);
    BRIDGE_DEBUG_PRINTLN("TX, len=%d crc=0x%04x\n", len, checksum);
  }
}

void TCPBridge::onPacketReceived(mesh::Packet* packet) {
  handleReceivedPacket(packet);
}

#endif
