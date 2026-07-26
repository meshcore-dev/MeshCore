#include "RS232Bridge.h"

#include <HardwareSerial.h>
#include <stdio.h>

#ifdef WITH_RS232_BRIDGE

RS232Bridge::RS232Bridge(NodePrefs *prefs, Stream &serial, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), _serial(&serial), _framer(MAX_PAYLOAD_SIZE),
      _rx_frames(RX_QUEUE_DEPTH, MAX_PAYLOAD_SIZE), _rx_no_packet(0), _rx_unparsed(0),
      _tx_duplicates(0), _tx_oversized(0) {}

void RS232Bridge::begin() {
  BRIDGE_DEBUG_PRINTLN("Initializing at %d baud...\n", _prefs->bridge_baud);
#if !defined(WITH_RS232_BRIDGE_RX) || !defined(WITH_RS232_BRIDGE_TX)
#error "WITH_RS232_BRIDGE_RX and WITH_RS232_BRIDGE_TX must be defined"
#endif

#if defined(ESP32)
  ((HardwareSerial *)_serial)->setPins(WITH_RS232_BRIDGE_RX, WITH_RS232_BRIDGE_TX);
#elif defined(NRF52_PLATFORM)
  // Tested with RAK_4631 and T114
  ((Uart *)_serial)->setPins(WITH_RS232_BRIDGE_RX, WITH_RS232_BRIDGE_TX);
#elif defined(RP2040_PLATFORM)
  ((SerialUART *)_serial)->setRX(WITH_RS232_BRIDGE_RX);
  ((SerialUART *)_serial)->setTX(WITH_RS232_BRIDGE_TX);
#elif defined(STM32_PLATFORM)
  ((HardwareSerial *)_serial)->setRx(WITH_RS232_BRIDGE_RX);
  ((HardwareSerial *)_serial)->setTx(WITH_RS232_BRIDGE_TX);
#else
#error RS232Bridge was not tested on the current platform
#endif
  ((HardwareSerial *)_serial)->begin(_prefs->bridge_baud);

  // Update bridge state
  _initialized = true;
}

void RS232Bridge::end() {
  BRIDGE_DEBUG_PRINTLN("Stopping...\n");
  ((HardwareSerial *)_serial)->end();

  // Update bridge state
  _initialized = false;

  // Drop anything half-received; the link is going away
  _framer.reset();
  uint8_t discard[MAX_PAYLOAD_SIZE];
  while (!_rx_frames.isEmpty()) {
    _rx_frames.pop(discard, sizeof(discard));
  }
}

void RS232Bridge::loop() {
  // Guard against uninitialized state
  if (_initialized == false) {
    return;
  }

  // Keep draining the UART even when the mesh cannot take packets: stalling
  // here would let the receive FIFO overrun mid-frame and corrupt it.
  while (_serial->available()) {
    const uint16_t payload_len = _framer.offer((uint8_t)_serial->read());
    if (payload_len > 0) {
      _rx_frames.push(_framer.payload(), payload_len);
    }
  }

  uint8_t payload[MAX_PAYLOAD_SIZE];

  for (uint8_t drained = 0; drained < RX_DRAIN_PER_LOOP && !_rx_frames.isEmpty(); drained++) {
    // Take a pool slot first: if the pool is empty the frame stays queued for a
    // later loop rather than being thrown away.
    mesh::Packet *pkt = _mgr->allocNew();
    if (pkt == nullptr) {
      _rx_no_packet++;
      break;
    }

    const size_t len = _rx_frames.pop(payload, sizeof(payload));
    if (len == 0) {
      _mgr->free(pkt);
      break;
    }

    BRIDGE_DEBUG_PRINTLN("RX, len=%d\n", (int)len);

    if (pkt->readFrom(payload, (uint8_t)len)) {
      onPacketReceived(pkt);  // takes ownership
    } else {
      BRIDGE_DEBUG_PRINTLN("RX failed to parse packet\n");
      _rx_unparsed++;
      _mgr->free(pkt);
    }
  }
}

void RS232Bridge::sendPacket(mesh::Packet *packet) {
  // Guard against uninitialized state
  if (_initialized == false) {
    return;
  }

  // First validate the packet pointer
  if (!packet) {
    BRIDGE_DEBUG_PRINTLN("TX invalid packet pointer\n");
    return;
  }

  if (_seen_packets.wasSeen(packet)) {
    _tx_duplicates++;
    return;
  }

  uint8_t blob[MAX_PAYLOAD_SIZE];
  const uint16_t blob_len = packet->writeTo(blob);

  uint8_t frame[MAX_SERIAL_PACKET_SIZE];
  const int frame_len = BridgeSerialFramer::encode(blob, blob_len, frame, sizeof(frame));
  if (frame_len < 0) {
    BRIDGE_DEBUG_PRINTLN("TX packet too large (payload=%d, max=%d)\n", blob_len,
                         (int)MAX_PAYLOAD_SIZE);
    _tx_oversized++;
    return;
  }

  _seen_packets.markSeen(packet);
  _serial->write(frame, (size_t)frame_len);

  BRIDGE_DEBUG_PRINTLN("TX, len=%d\n", blob_len);
}

void RS232Bridge::onPacketReceived(mesh::Packet *packet) {
  handleReceivedPacket(packet);
}

void RS232Bridge::getRxStats(char *dest, size_t dest_size) const {
  snprintf(dest, dest_size,
           "RX in=%u ok=%u dup=%u crc=%u len=%u noise=%u nopar=%u pool=%u qfull=%u hwm=%u/%u",
           (unsigned)_framer.getFramesDecoded(), (unsigned)_rx_delivered,
           (unsigned)_rx_duplicates, (unsigned)_framer.getChecksumErrors(),
           (unsigned)_framer.getLengthErrors(), (unsigned)_framer.getResyncBytes(),
           (unsigned)_rx_unparsed, (unsigned)_rx_no_packet,
           (unsigned)_rx_frames.getDroppedFull(), (unsigned)_rx_frames.getHighWaterMark(),
           (unsigned)_rx_frames.capacity());
}

void RS232Bridge::getTxStats(char *dest, size_t dest_size) const {
  snprintf(dest, dest_size, "TX dup=%u big=%u", (unsigned)_tx_duplicates,
           (unsigned)_tx_oversized);
}

void RS232Bridge::resetStats() {
  _rx_no_packet = 0;
  _rx_unparsed = 0;
  _tx_duplicates = 0;
  _tx_oversized = 0;
  _rx_delivered = 0;
  _rx_duplicates = 0;
  _framer.resetStats();
  _rx_frames.resetStats();
}

#endif
