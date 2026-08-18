#include "RS232Bridge.h"

#include <HardwareSerial.h>
#include <stdio.h>

#ifdef WITH_RS232_BRIDGE

RS232Bridge::RS232Bridge(NodePrefs *prefs, Stream &serial, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), _serial(&serial), _framer(MAX_PAYLOAD_SIZE),
      _rx_frames(RX_QUEUE_DEPTH, MAX_PAYLOAD_SIZE) {}

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
  _rx_frames.clear();
}

void RS232Bridge::loop() {
  // Guard against uninitialized state
  if (_initialized == false) {
    return;
  }

  // Keep draining the UART even when the mesh cannot take packets: stalling
  // here would let the receive FIFO overrun mid-frame and corrupt it. available()
  // takes the UART driver lock, so ask once per loop rather than once per byte.
  for (int pending = _serial->available(); pending > 0; pending--) {
    const uint16_t payload_len = _framer.offer((uint8_t)_serial->read());
    if (payload_len > 0) {
      _rx_frames.push(_framer.payload(), payload_len);
    }
  }

  // The framer has already stripped the serial framing, so the queued frames are
  // mesh packet blobs and the default unwrapFrame() passes them straight through.
  uint8_t payload[MAX_PAYLOAD_SIZE];
  drainRxFrames(_rx_frames, RX_DRAIN_PER_LOOP, payload, sizeof(payload));
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
  resetBaseStats();
  _framer.resetStats();
  _rx_frames.resetStats();
}

#endif
