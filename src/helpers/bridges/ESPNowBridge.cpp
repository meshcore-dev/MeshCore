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

ESPNowBridge::ESPNowBridge(mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(mgr, rtc), _rx_buffer_pos(0) {
  _instance = this;
}

void ESPNowBridge::begin() {
  // Initialize WiFi in station mode
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.printf("%s: ESPNOW BRIDGE: Error initializing ESP-NOW\n", getLogDateTime());
    return;
  }

  // Register callbacks
  esp_now_register_recv_cb(recv_cb);
  esp_now_register_send_cb(send_cb);

  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memset(&peerInfo, 0, sizeof(peerInfo));
  memset(peerInfo.peer_addr, 0xFF, ESP_NOW_ETH_ALEN); // Broadcast address
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.printf("%s: ESPNOW BRIDGE: Failed to add broadcast peer\n", getLogDateTime());
    return;
  }
}

void ESPNowBridge::loop() {
  // Nothing to do here - ESP-NOW is callback based
}

void ESPNowBridge::xorCrypt(uint8_t *data, size_t len) {
  size_t keyLen = strlen(_secret);
  for (size_t i = 0; i < len; i++) {
    data[i] ^= _secret[i % keyLen];
  }
}

void ESPNowBridge::onDataRecv(const uint8_t *mac, const uint8_t *data, int32_t len) {
  // Ignore packets that are too small to contain header + checksum
  if (len < (BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE)) {
#if MESH_PACKET_LOGGING
    Serial.printf("%s: ESPNOW BRIDGE: RX packet too small, len=%d\n", getLogDateTime(), len);
#endif
    return;
  }

  // Validate total packet size
  if (len > MAX_ESPNOW_PACKET_SIZE) {
#if MESH_PACKET_LOGGING
    Serial.printf("%s: ESPNOW BRIDGE: RX packet too large, len=%d\n", getLogDateTime(), len);
#endif
    return;
  }

  // Check packet header magic
  uint16_t received_magic = (data[0] << 8) | data[1];
  if (received_magic != BRIDGE_PACKET_MAGIC) {
#if MESH_PACKET_LOGGING
    Serial.printf("%s: ESPNOW BRIDGE: RX invalid magic 0x%04X\n", getLogDateTime(), received_magic);
#endif
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
#if MESH_PACKET_LOGGING
    Serial.printf("%s: ESPNOW BRIDGE: RX checksum mismatch, rcv=0x%04X\n", getLogDateTime(),
                  received_checksum);
#endif
    return;
  }

#if MESH_PACKET_LOGGING
  Serial.printf("%s: ESPNOW BRIDGE: RX, payload_len=%d\n", getLogDateTime(), payloadLen);
#endif

  // Create mesh packet
  mesh::Packet *pkt = _instance->_mgr->allocNew();
  if (!pkt) return;

  if (pkt->readFrom(decrypted + BRIDGE_CHECKSUM_SIZE, payloadLen)) {
    _instance->onPacketReceived(pkt);
  } else {
    _instance->_mgr->free(pkt);
  }
}

void ESPNowBridge::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Could add transmission error handling here if needed
}

void ESPNowBridge::onPacketReceived(mesh::Packet *packet) {
  handleReceivedPacket(packet);
}

void ESPNowBridge::onPacketTransmitted(mesh::Packet *packet) {
  // First validate the packet pointer
  if (!packet) {
#if MESH_PACKET_LOGGING
    Serial.printf("%s: ESPNOW BRIDGE: TX invalid packet pointer\n", getLogDateTime());
#endif
    return;
  }

  if (!_seen_packets.hasSeen(packet)) {

    // Create a temporary buffer just for size calculation and reuse for actual writing
    uint8_t sizingBuffer[MAX_PAYLOAD_SIZE];
    uint16_t meshPacketLen = packet->writeTo(sizingBuffer);

    // Check if packet fits within our maximum payload size
    if (meshPacketLen > MAX_PAYLOAD_SIZE) {
#if MESH_PACKET_LOGGING
      Serial.printf("%s: ESPNOW BRIDGE: TX packet too large (payload=%d, max=%d)\n", getLogDateTime(),
                    meshPacketLen, MAX_PAYLOAD_SIZE);
#endif
      return;
    }

    uint8_t buffer[MAX_ESPNOW_PACKET_SIZE];

    // Write magic header (2 bytes)
    buffer[0] = (BRIDGE_PACKET_MAGIC >> 8) & 0xFF;
    buffer[1] = BRIDGE_PACKET_MAGIC & 0xFF;

    // Write packet payload starting after magic header and checksum
    const size_t packetOffset = BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE;
    memcpy(buffer + packetOffset, sizingBuffer, meshPacketLen);

    // Calculate and add checksum (only of the payload)
    uint16_t checksum = fletcher16(buffer + packetOffset, meshPacketLen);
    buffer[2] = (checksum >> 8) & 0xFF; // High byte
    buffer[3] = checksum & 0xFF;        // Low byte

    // Encrypt payload and checksum (not including magic header)
    xorCrypt(buffer + BRIDGE_MAGIC_SIZE, meshPacketLen + BRIDGE_CHECKSUM_SIZE);

    // Total packet size: magic header + checksum + payload
    const size_t totalPacketSize = BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE + meshPacketLen;

    // Broadcast using ESP-NOW
    uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    esp_err_t result = esp_now_send(broadcastAddress, buffer, totalPacketSize);

#if MESH_PACKET_LOGGING
    if (result == ESP_OK) {
      Serial.printf("%s: ESPNOW BRIDGE: TX, len=%d\n", getLogDateTime(), meshPacketLen);
    } else {
      Serial.printf("%s: ESPNOW BRIDGE: TX FAILED!\n", getLogDateTime());
    }
#endif
  }
}

#endif
