#include "RS232Bridge.h"
#include <HardwareSerial.h>
#include <RTClib.h>

#ifdef WITH_RS232_BRIDGE

// Fletcher-16
// https://en.wikipedia.org/wiki/Fletcher%27s_checksum
inline static uint16_t fletcher16(const uint8_t *bytes, const size_t len) {
  uint8_t sum1 = 0, sum2 = 0;

  for (size_t i = 0; i < len; i++) {
    sum1 = (sum1 + bytes[i]) % 255;
    sum2 = (sum2 + sum1) % 255;
  }

  return (sum2 << 8) | sum1;
};

const char* RS232Bridge::getLogDateTime() {
  static char tmp[32];
  uint32_t now = _rtc->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(), dt.year());
  return tmp;
}

RS232Bridge::RS232Bridge(Stream& serial, mesh::PacketManager* mgr, mesh::RTCClock* rtc) : _serial(&serial), _mgr(mgr), _rtc(rtc) {}

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
  ((HardwareSerial*)_serial)->begin(115200);
}

void RS232Bridge::onPacketTransmitted(mesh::Packet* packet) {
  if (!_seen_packets.hasSeen(packet)) {
    uint8_t buffer[MAX_SERIAL_PACKET_SIZE];
    uint16_t len = packet->writeTo(buffer + 4);

    buffer[0] = (SERIAL_PKT_MAGIC >> 8) & 0xFF;
    buffer[1] = SERIAL_PKT_MAGIC & 0xFF;
    buffer[2] = (len >> 8) & 0xFF;
    buffer[3] = len & 0xFF;

    uint16_t checksum = fletcher16(buffer + 4, len);
    buffer[4 + len] = (checksum >> 8) & 0xFF;
    buffer[5 + len] = checksum & 0xFF;

    _serial->write(buffer, len + SERIAL_OVERHEAD);

#if MESH_PACKET_LOGGING
    Serial.printf("%s: BRIDGE: TX, len=%d crc=0x%04x\n", getLogDateTime(), len, checksum);
#endif
  }
}

void RS232Bridge::loop() {
  while (_serial->available()) {
    uint8_t b = _serial->read();

    if (_rx_buffer_pos < 2) {
      // Waiting for magic word
      if ((_rx_buffer_pos == 0 && b == ((SERIAL_PKT_MAGIC >> 8) & 0xFF)) ||
          (_rx_buffer_pos == 1 && b == (SERIAL_PKT_MAGIC & 0xFF))) {
        _rx_buffer[_rx_buffer_pos++] = b;
      } else {
        _rx_buffer_pos = 0;
      }
    } else {
      // Reading length, payload, and checksum
      _rx_buffer[_rx_buffer_pos++] = b;

      if (_rx_buffer_pos >= 4) {
        uint16_t len = (_rx_buffer[2] << 8) | _rx_buffer[3];
        if (len > (MAX_TRANS_UNIT + 1)) {
          _rx_buffer_pos = 0; // Invalid length, reset
          return;
        }

        if (_rx_buffer_pos == len + SERIAL_OVERHEAD) { // Full packet received
          uint16_t checksum = (_rx_buffer[4 + len] << 8) | _rx_buffer[5 + len];
          if (checksum == fletcher16(_rx_buffer + 4, len)) {
#if MESH_PACKET_LOGGING
            Serial.printf("%s: BRIDGE: RX, len=%d crc=0x%04x\n", getLogDateTime(), len, checksum);
#endif
            mesh::Packet* pkt = _mgr->allocNew();
            if (pkt) {
              pkt->readFrom(_rx_buffer + 4, len);
              onPacketReceived(pkt);
            }
          }
          _rx_buffer_pos = 0; // Reset for next packet
        }
      }
    }
  }
}

void RS232Bridge::onPacketReceived(mesh::Packet* packet) {
  if (!_seen_packets.hasSeen(packet)) {
    _mgr->queueInbound(packet, 0);
  } else {
    _mgr->free(packet);
  }
}

#endif
