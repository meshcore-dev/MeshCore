#include "RS232Bridge.h"

#include <HardwareSerial.h>

#ifdef WITH_RS232_BRIDGE

RS232Bridge::RS232Bridge(Stream &serial, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(mgr, rtc), _serial(&serial) {}

void RS232Bridge::begin() {
#if !defined(WITH_RS232_BRIDGE_RX) || !defined(WITH_RS232_BRIDGE_TX)
#error "WITH_RS232_BRIDGE_RX and WITH_RS232_BRIDGE_TX must be defined"
#endif

#if defined(ESP32)
  ((HardwareSerial *)_serial)->setPins(WITH_RS232_BRIDGE_RX, WITH_RS232_BRIDGE_TX);
#elif defined(NRF52_PLATFORM)
  ((HardwareSerial *)_serial)->setPins(WITH_RS232_BRIDGE_RX, WITH_RS232_BRIDGE_TX);
#elif defined(RP2040_PLATFORM)
  ((SerialUART *)_serial)->setRX(WITH_RS232_BRIDGE_RX);
  ((SerialUART *)_serial)->setTX(WITH_RS232_BRIDGE_TX);
#elif defined(STM32_PLATFORM)
  ((HardwareSerial *)_serial)->setRx(WITH_RS232_BRIDGE_RX);
  ((HardwareSerial *)_serial)->setTx(WITH_RS232_BRIDGE_TX);
#else
#error RS232Bridge was not tested on the current platform
#endif
  ((HardwareSerial *)_serial)->begin(115200);
}

void RS232Bridge::onPacketTransmitted(mesh::Packet *packet) {
  // First validate the packet pointer
  if (!packet) {
#if MESH_PACKET_LOGGING
    Serial.printf("%s: RS232 BRIDGE: TX invalid packet pointer\n", getLogDateTime());
#endif
    return;
  }

  if (!_seen_packets.hasSeen(packet)) {

    uint8_t buffer[MAX_SERIAL_PACKET_SIZE];
    uint16_t len = packet->writeTo(buffer + 4);

    // Check if packet fits within our maximum payload size
    if (len > (MAX_TRANS_UNIT + 1)) {
#if MESH_PACKET_LOGGING
      Serial.printf("%s: RS232 BRIDGE: TX packet too large (payload=%d, max=%d)\n", getLogDateTime(), len,
                    MAX_TRANS_UNIT + 1);
#endif
      return;
    }

    // Build packet header
    buffer[0] = (BRIDGE_PACKET_MAGIC >> 8) & 0xFF; // Magic high byte
    buffer[1] = BRIDGE_PACKET_MAGIC & 0xFF;        // Magic low byte
    buffer[2] = (len >> 8) & 0xFF;                 // Length high byte
    buffer[3] = len & 0xFF;                        // Length low byte

    // Calculate checksum over the payload
    uint16_t checksum = fletcher16(buffer + 4, len);
    buffer[4 + len] = (checksum >> 8) & 0xFF; // Checksum high byte
    buffer[5 + len] = checksum & 0xFF;        // Checksum low byte

    // Send complete packet
    _serial->write(buffer, len + SERIAL_OVERHEAD);

#if MESH_PACKET_LOGGING
    Serial.printf("%s: RS232 BRIDGE: TX, len=%d crc=0x%04x\n", getLogDateTime(), len, checksum);
#endif
  }
}

void RS232Bridge::loop() {
  while (_serial->available()) {
    uint8_t b = _serial->read();

    if (_rx_buffer_pos < 2) {
      // Waiting for magic word
      if ((_rx_buffer_pos == 0 && b == ((BRIDGE_PACKET_MAGIC >> 8) & 0xFF)) ||
          (_rx_buffer_pos == 1 && b == (BRIDGE_PACKET_MAGIC & 0xFF))) {
        _rx_buffer[_rx_buffer_pos++] = b;
      } else {
        // Invalid magic byte, reset and start over
        _rx_buffer_pos = 0;
        // Check if this byte could be the start of a new magic word
        if (b == ((BRIDGE_PACKET_MAGIC >> 8) & 0xFF)) {
          _rx_buffer[_rx_buffer_pos++] = b;
        }
      }
    } else {
      // Reading length, payload, and checksum
      _rx_buffer[_rx_buffer_pos++] = b;

      if (_rx_buffer_pos >= 4) {
        uint16_t len = (_rx_buffer[2] << 8) | _rx_buffer[3];

        // Validate length field
        if (len > (MAX_TRANS_UNIT + 1)) {
#if MESH_PACKET_LOGGING
          Serial.printf("%s: RS232 BRIDGE: RX invalid length %d, resetting\n", getLogDateTime(), len);
#endif
          _rx_buffer_pos = 0; // Invalid length, reset
          continue;
        }

        if (_rx_buffer_pos == len + SERIAL_OVERHEAD) { // Full packet received
          uint16_t received_checksum = (_rx_buffer[4 + len] << 8) | _rx_buffer[5 + len];

          if (validateChecksum(_rx_buffer + 4, len, received_checksum)) {
#if MESH_PACKET_LOGGING
            Serial.printf("%s: RS232 BRIDGE: RX, len=%d crc=0x%04x\n", getLogDateTime(), len,
                          received_checksum);
#endif
            mesh::Packet *pkt = _mgr->allocNew();
            if (pkt) {
              if (pkt->readFrom(_rx_buffer + 4, len)) {
                onPacketReceived(pkt);
              } else {
#if MESH_PACKET_LOGGING
                Serial.printf("%s: RS232 BRIDGE: RX failed to parse packet\n", getLogDateTime());
#endif
                _mgr->free(pkt);
              }
            } else {
#if MESH_PACKET_LOGGING
              Serial.printf("%s: RS232 BRIDGE: RX failed to allocate packet\n", getLogDateTime());
#endif
            }
          } else {
#if MESH_PACKET_LOGGING
            Serial.printf("%s: RS232 BRIDGE: RX checksum mismatch, rcv=0x%04x\n", getLogDateTime(),
                          received_checksum);
#endif
          }
          _rx_buffer_pos = 0; // Reset for next packet
        }
      }
    }
  }
}

void RS232Bridge::onPacketReceived(mesh::Packet *packet) {
  handleReceivedPacket(packet);
}

#endif
