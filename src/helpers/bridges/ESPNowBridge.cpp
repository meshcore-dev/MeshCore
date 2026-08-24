#include "ESPNowBridge.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <stdio.h>

#ifdef WITH_ESPNOW_BRIDGE

static const uint8_t BROADCAST_ADDRESS[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Static member to handle callbacks
ESPNowBridge *ESPNowBridge::_instance = nullptr;

// Static callback wrappers. Both run on the Wi-Fi task, not the Arduino loop
// task, so they must not touch the packet pool, the inbound queue or the
// seen-packet table: none of those are synchronised.
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void ESPNowBridge::recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (_instance == nullptr || len <= 0) return;
  _instance->_rx_frames.push(data, (size_t)len);
}
#else
void ESPNowBridge::recv_cb(const uint8_t *mac, const uint8_t *data, int len) {
  if (_instance == nullptr || len <= 0) return;
  _instance->_rx_frames.push(data, (size_t)len);
}
#endif

void ESPNowBridge::send_cb(const uint8_t *mac, esp_now_send_status_t status) {
  if (_instance == nullptr) return;
  _instance->_tx_frames.onSendComplete(status == ESP_NOW_SEND_SUCCESS);
}

ESPNowBridge::ESPNowBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), _rx_frames(RX_QUEUE_DEPTH, MAX_ESPNOW_PACKET_SIZE),
      _tx_frames(TX_QUEUE_DEPTH, MAX_ESPNOW_PACKET_SIZE, TX_MAX_ATTEMPTS, TX_ACK_TIMEOUT_MS,
                 TX_RETRY_DELAY_MS),
      _rx_invalid(0) {
  _instance = this;
  _tx_frames.setSender(this);
}

void ESPNowBridge::begin() {
  BRIDGE_DEBUG_PRINTLN("Initializing...\n");

  // Initialize WiFi in station mode
  WiFi.mode(WIFI_STA);

  // With power save at its default the radio dozes and misses broadcasts.
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Set Wi-Fi channel
  if (esp_wifi_set_channel(_prefs->bridge_channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Error setting WIFI channel to %d\n", _prefs->bridge_channel);
    return;
  }

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Error initializing ESP-NOW\n");
    return;
  }

  // Register callbacks
  esp_now_register_recv_cb(recv_cb);
  esp_now_register_send_cb(send_cb);

  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, BROADCAST_ADDRESS, ESP_NOW_ETH_ALEN);
  // 0 = use the interface channel set above. An explicit value that disagreed
  // with it would make ESP-NOW hop per send.
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Failed to add broadcast peer\n");
    return;
  }

  // Update bridge state
  _initialized = true;
}

void ESPNowBridge::end() {
  BRIDGE_DEBUG_PRINTLN("Stopping...\n");

  _initialized = false;

  // Unregister before tearing down, so nothing can push into the queues
  esp_now_register_recv_cb(nullptr);
  esp_now_register_send_cb(nullptr);

  // Remove broadcast peer
  if (esp_now_del_peer(BROADCAST_ADDRESS) != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Error removing broadcast peer\n");
  }

  // Deinitialize ESP-NOW
  if (esp_now_deinit() != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Error deinitializing ESP-NOW\n");
  }

  // Turn off WiFi
  WiFi.mode(WIFI_OFF);

  // Drop anything still buffered; it belongs to a radio that no longer exists
  _tx_frames.reset();
  _rx_frames.clear();
}

void ESPNowBridge::loop() {
  if (!_initialized) return;

  uint8_t frame[MAX_ESPNOW_PACKET_SIZE];
  drainRxFrames(_rx_frames, RX_DRAIN_PER_LOOP, frame, sizeof(frame));

  _tx_frames.loop(millis());
}

const uint8_t *ESPNowBridge::unwrapFrame(const uint8_t *frame, size_t frame_len, size_t &blob_len) {
  const int payload_len =
      BridgeCodec::decode(frame, frame_len, _prefs->bridge_secret, _rx_blob, sizeof(_rx_blob));
  if (payload_len <= 0) {
    // Wrong magic, bad checksum, or another network's secret
    BRIDGE_DEBUG_PRINTLN("RX invalid frame, len=%d\n", (int)frame_len);
    _rx_invalid++;
    return nullptr;
  }

  blob_len = (size_t)payload_len;
  return _rx_blob;
}

void ESPNowBridge::sendPacket(mesh::Packet *packet) {
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

  // writeTo() can emit up to MAX_TRANS_UNIT bytes, so size the buffer for that
  // and check whether it fits a frame afterwards.
  uint8_t blob[MAX_TRANS_UNIT + 1];
  const uint16_t blob_len = packet->writeTo(blob);

  uint8_t frame[MAX_ESPNOW_PACKET_SIZE];
  const int frame_len =
      BridgeCodec::encode(blob, blob_len, _prefs->bridge_secret, frame, sizeof(frame));
  if (frame_len < 0) {
    BRIDGE_DEBUG_PRINTLN("TX packet too large (payload=%d, max=%d)\n", blob_len,
                         (int)MAX_PAYLOAD_SIZE);
    _tx_oversized++;
    return;
  }

  if (_tx_frames.enqueue(frame, (size_t)frame_len)) {
    // Mark once queued, not once transmitted: a frame awaiting its turn must
    // already suppress the same packet arriving back over the bridge. Marking
    // only on success leaves a dropped packet eligible if the mesh resends it.
    _seen_packets.markSeen(packet);
    BRIDGE_DEBUG_PRINTLN("TX queued, len=%d\n", blob_len);
  } else {
    BRIDGE_DEBUG_PRINTLN("TX queue full, len=%d\n", blob_len);
  }
}

bool ESPNowBridge::sendFrame(const uint8_t *frame, size_t len) {
  return esp_now_send(BROADCAST_ADDRESS, frame, len) == ESP_OK;
}

void ESPNowBridge::onPacketReceived(mesh::Packet *packet) {
  handleReceivedPacket(packet);
}

void ESPNowBridge::getRxStats(char *dest, size_t dest_size) const {
  snprintf(dest, dest_size,
           "RX in=%u ok=%u dup=%u bad=%u nopar=%u pool=%u qfull=%u hwm=%u/%u",
           (unsigned)_rx_frames.getPushed(), (unsigned)_rx_delivered, (unsigned)_rx_duplicates,
           (unsigned)_rx_invalid, (unsigned)_rx_unparsed, (unsigned)_rx_no_packet,
           (unsigned)_rx_frames.getDroppedFull(), (unsigned)_rx_frames.getHighWaterMark(),
           (unsigned)_rx_frames.capacity());
}

void ESPNowBridge::getTxStats(char *dest, size_t dest_size) const {
  snprintf(dest, dest_size,
           "TX ok=%u dup=%u big=%u rty=%u ref=%u tmo=%u fail=%u qfull=%u hwm=%u/%u",
           (unsigned)_tx_frames.getSent(), (unsigned)_tx_duplicates, (unsigned)_tx_oversized,
           (unsigned)_tx_frames.getRetries(), (unsigned)_tx_frames.getRadioRefusals(),
           (unsigned)_tx_frames.getTimeouts(), (unsigned)_tx_frames.getFailed(),
           (unsigned)_tx_frames.queue().getDroppedFull(),
           (unsigned)_tx_frames.queue().getHighWaterMark(),
           (unsigned)_tx_frames.queue().capacity());
}

void ESPNowBridge::resetStats() {
  resetBaseStats();
  _rx_invalid = 0;
  _rx_frames.resetStats();
  _tx_frames.resetStats();
}

#endif
