#include "BridgeBase.h"

#include <Arduino.h>

bool BridgeBase::isRunning() const {
  return _initialized;
}

const char *BridgeBase::getLogDateTime() {
  static char tmp[32];
  uint32_t now = _rtc->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

void BridgeBase::resetBaseStats() {
  _rx_delivered = 0;
  _rx_duplicates = 0;
  _rx_no_packet = 0;
  _rx_unparsed = 0;
  _tx_duplicates = 0;
  _tx_oversized = 0;
}

void BridgeBase::drainRxFrames(BridgeFrameQueue &queue, uint8_t max_frames, uint8_t *scratch,
                               size_t scratch_cap) {
  for (uint8_t drained = 0; drained < max_frames && !queue.isEmpty(); drained++) {
    // Take a pool slot first: if the pool is empty the frame stays queued for a
    // later loop, costing latency instead of a message.
    mesh::Packet *pkt = _mgr->allocNew();
    if (pkt == nullptr) {
      _rx_no_packet++;
      break;
    }

    const size_t frame_len = queue.pop(scratch, scratch_cap);
    if (frame_len == 0) {
      _mgr->free(pkt);
      break;
    }

    size_t blob_len = 0;
    const uint8_t *blob = unwrapFrame(scratch, frame_len, blob_len);
    if (blob == nullptr) {
      _mgr->free(pkt);  // the override counted why
      continue;
    }

    BRIDGE_DEBUG_PRINTLN("RX, len=%d\n", (int)blob_len);

    // readFrom() takes a uint8_t length, and a mesh packet can never be longer
    // than MAX_TRANS_UNIT + 1 anyway.
    if (blob_len > 0 && blob_len <= 255 && pkt->readFrom(blob, (uint8_t)blob_len)) {
      onPacketReceived(pkt);  // takes ownership
    } else {
      BRIDGE_DEBUG_PRINTLN("RX failed to parse packet\n");
      _rx_unparsed++;
      _mgr->free(pkt);
    }
  }
}

void BridgeBase::handleReceivedPacket(mesh::Packet *packet) {
  // Guard against uninitialized state
  if (_initialized == false) {
    BRIDGE_DEBUG_PRINTLN("RX packet received before initialization\n");
    _mgr->free(packet);
    return;
  }

  if (!_seen_packets.wasSeen(packet)) {
    _seen_packets.markSeen(packet);
    // bridge_delay provides a buffer to prevent immediate processing conflicts in the mesh network.
    _mgr->queueInbound(packet, millis() + _prefs->bridge_delay);
    _rx_delivered++;
  } else {
    _mgr->free(packet);
    _rx_duplicates++;
  }
}
