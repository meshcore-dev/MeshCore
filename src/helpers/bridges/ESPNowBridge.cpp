#include "ESPNowBridge.h"

#include <WiFi.h>
#include <esp_wifi.h>

#ifdef WITH_ESPNOW_BRIDGE

// Static member to handle callbacks
ESPNowBridge *ESPNowBridge::_instance = nullptr;

// Static callback wrappers
void ESPNowBridge::recv_cb(const uint8_t *mac, const uint8_t *data, int32_t len) {
  if (_instance) {
    _instance->onDataRecv(mac, data, len);
  }
}

void ESPNowBridge::send_cb(const uint8_t *mac, esp_now_send_status_t status) {
  if (_instance) {
    _instance->onDataSent(mac, status);
  }
}

ESPNowBridge::ESPNowBridge(BridgePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), _rx_buffer_pos(0), _self_hash(0), _have_self_hash(false),
      _last_announce(0), _inflight_next(0) {
  _instance = this;
  memset(_peers, 0, sizeof(_peers));
  memset(_inflight, 0, sizeof(_inflight));
  memset(&_counters, 0, sizeof(_counters));
}

void ESPNowBridge::begin() {
  BRIDGE_DEBUG_PRINTLN("Initializing...\n");

  // Initialize WiFi in station mode
  WiFi.mode(WIFI_STA);
  
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
  memset(peerInfo.peer_addr, 0xFF, ESP_NOW_ETH_ALEN); // Broadcast address
  peerInfo.channel = _prefs->bridge_channel;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Failed to add broadcast peer\n");
    return;
  }

  // Update bridge state
  _initialized = true;

  // Announce immediately, then on the interval from loop(): a peer only sends
  // over this lane once it has heard that this node is on it.
  if (_have_self_hash) {
    sendAnnounce(millis());
  }
}

void ESPNowBridge::end() {
  BRIDGE_DEBUG_PRINTLN("Stopping...\n");

  // Remove broadcast peer
  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  if (esp_now_del_peer(broadcastAddress) != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Error removing broadcast peer\n");
  }

  // Unregister callbacks
  esp_now_register_recv_cb(nullptr);
  esp_now_register_send_cb(nullptr);

  // Deinitialize ESP-NOW
  if (esp_now_deinit() != ESP_OK) {
    BRIDGE_DEBUG_PRINTLN("Error deinitializing ESP-NOW\n");
  }

  // Turn off WiFi
  WiFi.mode(WIFI_OFF);

  // Update bridge state
  _initialized = false;
}

void ESPNowBridge::loop() {
  // Receiving is callback based; the only work here is telling peers this lane is
  // still up. A silent bridge stops being a fast-lane destination, and traffic for
  // it falls back to the radio.
  if (!_initialized || !_have_self_hash) {
    return;
  }
  uint32_t now = millis();
  if ((now - _last_announce) >= BRIDGE_ANNOUNCE_INTERVAL_MS) {
    sendAnnounce(now);
  }
}

void ESPNowBridge::notePeer(const uint8_t *mac, uint8_t hash, uint32_t now) {
  PeerEntry *free_slot = NULL;
  PeerEntry *oldest = NULL;
  for (size_t i = 0; i < MAX_BRIDGE_PEERS; i++) {
    if (_peers[i].used && _peers[i].hash == hash && memcmp(_peers[i].mac, mac, ESP_NOW_ETH_ALEN) == 0) {
      _peers[i].last_seen = now;
      return;
    }
    if (!_peers[i].used) {
      if (free_slot == NULL) {
        free_slot = &_peers[i];
      }
    } else if (oldest == NULL || _peers[i].last_seen < oldest->last_seen) {
      oldest = &_peers[i];
    }
  }

  PeerEntry *slot = free_slot ? free_slot : oldest;
  memcpy(slot->mac, mac, ESP_NOW_ETH_ALEN);
  slot->hash = hash;
  slot->last_seen = now;
  slot->used = true;
}

bool ESPNowBridge::peerReachable(uint8_t hash, uint32_t now) {
  uint32_t newest = 0;
  for (size_t i = 0; i < MAX_BRIDGE_PEERS; i++) {
    if (_peers[i].used && _peers[i].hash == hash && _peers[i].last_seen > newest) {
      newest = _peers[i].last_seen;
    }
  }
  return newest != 0 && (now - newest) < BRIDGE_PEER_TTL_MS;
}

void ESPNowBridge::sendAnnounce(uint32_t now) {
  uint8_t hash = _self_hash;
  sendFrame(BRIDGE_ANNOUNCE_MAGIC, &hash, 1, true);
  _last_announce = now;
}

void ESPNowBridge::handleAnnounce(const uint8_t *mac, const uint8_t *data, int32_t len, uint32_t now) {
  // Same envelope as a packet, one byte of payload: the sender's mesh hash.
  if (len != (int32_t)(BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE + 1)) {
    _counters.rx_bad++;
    return;
  }

  uint8_t decrypted[BRIDGE_CHECKSUM_SIZE + 1];
  memcpy(decrypted, data + BRIDGE_MAGIC_SIZE, BRIDGE_CHECKSUM_SIZE + 1);
  xorCrypt(decrypted, BRIDGE_CHECKSUM_SIZE + 1);

  uint16_t received_checksum = (decrypted[0] << 8) | decrypted[1];
  if (!validateChecksum(decrypted + BRIDGE_CHECKSUM_SIZE, 1, received_checksum)) {
    _counters.rx_bad++;
    return;
  }

  _counters.rx_announce++;
  notePeer(mac, decrypted[BRIDGE_CHECKSUM_SIZE], now);
}

void ESPNowBridge::notePeerFromPacket(const uint8_t *mac, const mesh::Packet *packet, uint32_t now) {
  if (!packet->isRouteDirect() || packet->getPathHashCount() != 0 || packet->payload_len < 2) {
    return;   // not a one-hop delivery from the sender: its hash is not this packet's
  }
  uint8_t type = packet->getPayloadType();
  if (type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE
      || type == PAYLOAD_TYPE_PATH) {
    // [dest][src][...]: the sender's hash. This only *refreshes* a peer the
    // announcements already established, and only when the hash arrives from the
    // same MAC - so a packet relayed here by someone else cannot introduce a peer
    // that never announced itself on this lane.
    uint8_t hash = packet->payload[1];
    for (size_t i = 0; i < MAX_BRIDGE_PEERS; i++) {
      if (_peers[i].used && _peers[i].hash == hash && memcmp(_peers[i].mac, mac, ESP_NOW_ETH_ALEN) == 0) {
        _peers[i].last_seen = now;
        return;
      }
    }
  }
}

void ESPNowBridge::sendFrame(uint16_t magic, const uint8_t *payload, size_t payload_len, bool is_announce) {
  if (!_initialized || payload_len > MAX_PAYLOAD_SIZE) {
    return;
  }

  uint8_t buffer[MAX_ESPNOW_PACKET_SIZE];
  buffer[0] = (magic >> 8) & 0xFF;
  buffer[1] = magic & 0xFF;

  const size_t packetOffset = BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE;
  memcpy(buffer + packetOffset, payload, payload_len);

  uint16_t checksum = fletcher16(buffer + packetOffset, payload_len);
  buffer[2] = (checksum >> 8) & 0xFF;
  buffer[3] = checksum & 0xFF;
  xorCrypt(buffer + BRIDGE_MAGIC_SIZE, payload_len + BRIDGE_CHECKSUM_SIZE);

  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_err_t result = esp_now_send(broadcastAddress, buffer, BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE + payload_len);
  if (result != ESP_OK) {
    _counters.tx_failed++;
    BRIDGE_DEBUG_PRINTLN("TX FAILED (magic 0x%04X, len=%d)!\n", magic, (uint32_t)payload_len);
  } else if (is_announce) {
    _counters.tx_announce++;
  } else {
    _counters.tx++;
  }
}

void ESPNowBridge::recordInflight(const mesh::Packet *packet, uint32_t now) {
  InflightEntry *slot = &_inflight[_inflight_next];
  packet->calculatePacketHash(slot->hash);
  slot->received = now;
  slot->used = true;
  _inflight_next = (_inflight_next + 1) % INFLIGHT_SLOTS;
}

void ESPNowBridge::completeInflight(const mesh::Packet *packet, uint32_t now) {
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  for (size_t i = 0; i < INFLIGHT_SLOTS; i++) {
    if (_inflight[i].used && memcmp(_inflight[i].hash, hash, MAX_HASH_SIZE) == 0) {
      uint32_t elapsed = now - _inflight[i].received;
      _inflight[i].used = false;
      if (elapsed > _counters.inject_to_process_max_ms) {
        _counters.inject_to_process_max_ms = elapsed;
      }
      _counters.inject_to_process_last_ms = elapsed;
      return;
    }
  }
}

uint32_t ESPNowBridge::getInjectDelayMs(const mesh::Packet *packet) {
  // A directly routed packet with no hops is this node's own unicast traffic: the
  // mesh, not a repeater, is its destination, and nothing here re-broadcasts it if
  // the radio's copy is still on its way. Flood traffic keeps the configured hold,
  // which is what stops a bridged flood copy from provoking a second reaction.
  if (packet->isRouteDirect() && packet->getPathHashCount() == 0) {
    return 0;
  }
  return _prefs->bridge_delay;
}

bool ESPNowBridge::claimOutboundPacket(mesh::Packet *packet) {
  if (!_initialized || !packet || !_prefs->bridge_enabled) {
    return false;
  }
  // Only a bridge that mirrors what this node *transmits* selects transports for
  // it; a bridge configured to mirror receptions leaves the radio as the
  // transport of record for this node's own traffic.
  if (_prefs->bridge_pkt_src != 0) {
    return false;
  }

  uint8_t type = packet->getPayloadType();
  if (packet->isRouteDirect() && packet->getPathHashCount() == 0 && packet->payload_len > 1
      && (type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_REQ
          || type == PAYLOAD_TYPE_RESPONSE || type == PAYLOAD_TYPE_PATH)) {
    // These payloads start with the destination's hash (then this node's), so the
    // destination is known: use the lane only if that peer is really on it.
    if (peerReachable(packet->payload[0], millis())) {
      sendPacket(packet);
      _counters.fastlane_tx++;
      return true;   // the radio must not transmit this: no airtime is owed
    }
    _counters.fastlane_miss++;
  }

  // Everything else keeps the radio as the transport of record, but leaves here
  // first: mirroring before the transmit is what lets a nearby peer's copy arrive
  // without waiting for the airtime the old post-transmit mirror paid for.
  sendPacket(packet);
  _counters.mirror_tx++;
  return false;
}

void ESPNowBridge::onInboundPacketProcessed(mesh::Packet *packet) {
  completeInflight(packet, millis());
}

size_t ESPNowBridge::writeStats(uint8_t *dest, size_t max_len) {
  // Wire layout, after the [RESP_CODE_STATS][STATS_TYPE_BRIDGE] header:
  //   18 x uint32 little-endian counters, then
  //   uint8 bridge_delay_ms, uint8 peers_known, uint8 running, uint8 self_hash
  const size_t needed = 4 * 18 + 4;
  if (max_len < needed) {
    return 0;
  }
  uint32_t values[18] = {
    _counters.tx, _counters.tx_announce, _counters.tx_failed, _counters.tx_done,
    _counters.tx_error, _counters.rx, _counters.rx_announce, _counters.rx_bad,
    _counters.fastlane_tx, _counters.mirror_tx, _counters.fastlane_miss,
    _counters.rx_to_queue_last_ms, _counters.rx_to_queue_max_ms,
    _counters.inject_to_process_last_ms, _counters.inject_to_process_max_ms,
    _dropped_dup, _dropped_uninit + _counters.dropped_nospace, _injected,
  };
  size_t i = 0;
  for (size_t n = 0; n < 18; n++) {
    memcpy(&dest[i], &values[n], 4); i += 4;
  }
  uint8_t peers = 0;
  uint32_t now = millis();
  for (size_t n = 0; n < MAX_BRIDGE_PEERS; n++) {
    if (_peers[n].used && (now - _peers[n].last_seen) < BRIDGE_PEER_TTL_MS) {
      peers++;
    }
  }
  dest[i++] = _prefs->bridge_delay > 255 ? 255 : (uint8_t)_prefs->bridge_delay;
  dest[i++] = peers;
  dest[i++] = _initialized ? 1 : 0;
  dest[i++] = _have_self_hash ? _self_hash : 0;
  return i;
}

void ESPNowBridge::xorCrypt(uint8_t *data, size_t len) {
  size_t keyLen = strlen(_prefs->bridge_secret);
  for (size_t i = 0; i < len; i++) {
    data[i] ^= _prefs->bridge_secret[i % keyLen];
  }
}

void ESPNowBridge::onDataRecv(const uint8_t *mac, const uint8_t *data, int32_t len) {
  uint32_t now = millis();

  // Ignore packets that are too small to contain header + checksum
  if (len < (BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE)) {
    BRIDGE_DEBUG_PRINTLN("RX packet too small, len=%d\n", len);
    _counters.rx_bad++;
    return;
  }

  // Validate total packet size
  if (len > MAX_ESPNOW_PACKET_SIZE) {
    BRIDGE_DEBUG_PRINTLN("RX packet too large, len=%d\n", len);
    _counters.rx_bad++;
    return;
  }

  // Check packet header magic
  uint16_t received_magic = (data[0] << 8) | data[1];
  if (received_magic == BRIDGE_ANNOUNCE_MAGIC) {
    handleAnnounce(mac, data, len, now);
    return;
  }
  if (received_magic != BRIDGE_PACKET_MAGIC) {
    BRIDGE_DEBUG_PRINTLN("RX invalid magic 0x%04X\n", received_magic);
    _counters.rx_bad++;
    return;
  }

  // Make a copy we can decrypt
  uint8_t decrypted[MAX_ESPNOW_PACKET_SIZE];
  const size_t encryptedDataLen = len - BRIDGE_MAGIC_SIZE;
  memcpy(decrypted, data + BRIDGE_MAGIC_SIZE, encryptedDataLen);

  // Try to decrypt (checksum + payload)
  xorCrypt(decrypted, encryptedDataLen);

  // Validate checksum
  uint16_t received_checksum = (decrypted[0] << 8) | decrypted[1];
  const size_t payloadLen = encryptedDataLen - BRIDGE_CHECKSUM_SIZE;

  if (!validateChecksum(decrypted + BRIDGE_CHECKSUM_SIZE, payloadLen, received_checksum)) {
    // Failed to decrypt - likely from a different network
    BRIDGE_DEBUG_PRINTLN("RX checksum mismatch, rcv=0x%04X\n", received_checksum);
    _counters.rx_bad++;
    return;
  }

  BRIDGE_DEBUG_PRINTLN("RX, payload_len=%d\n", payloadLen);
  _counters.rx++;

  // Create mesh packet
  mesh::Packet *pkt = _instance->_mgr->allocNew();
  if (!pkt) {
    _counters.dropped_nospace++;
    return;
  }

  if (pkt->readFrom(decrypted + BRIDGE_CHECKSUM_SIZE, payloadLen)) {
    _instance->notePeerFromPacket(mac, pkt, now);
    _instance->recordInflight(pkt, now);
    _instance->onPacketReceived(pkt);
  } else {
    _counters.rx_bad++;
    _instance->_mgr->free(pkt);
  }
}

void ESPNowBridge::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    _counters.tx_done++;
  } else {
    _counters.tx_error++;
  }
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

  if (!_seen_packets.wasSeen(packet)) {
    _seen_packets.markSeen(packet);
    // Create a temporary buffer just for size calculation and reuse for actual writing
    uint8_t sizingBuffer[MAX_PAYLOAD_SIZE];
    uint16_t meshPacketLen = packet->writeTo(sizingBuffer);

    // Check if packet fits within our maximum payload size
    if (meshPacketLen > MAX_PAYLOAD_SIZE) {
      BRIDGE_DEBUG_PRINTLN("TX packet too large (payload=%d, max=%d)\n", meshPacketLen,
                           MAX_PAYLOAD_SIZE);
      _counters.dropped_nospace++;
      return;
    }

    sendFrame(BRIDGE_PACKET_MAGIC, sizingBuffer, meshPacketLen, false);
  }
}

void ESPNowBridge::onPacketReceived(mesh::Packet *packet) {
  uint32_t started = millis();
  _counters.inject_delay_last_ms = getInjectDelayMs(packet);
  handleReceivedPacket(packet);

  // Everything this bridge does between the ESP-NOW callback and the mesh's
  // inbound queue: decrypt, validate, parse, and hand over. Reported so the next
  // measurement can tell the bridge's own cost from the mesh's scheduling.
  uint32_t elapsed = millis() - started;
  if (elapsed > _counters.rx_to_queue_max_ms) {
    _counters.rx_to_queue_max_ms = elapsed;
  }
  _counters.rx_to_queue_last_ms = elapsed;
}

#endif
