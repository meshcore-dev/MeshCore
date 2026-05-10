#include "ESPNOWRadio.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

  static uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  static esp_now_peer_info_t peerInfo;
  static volatile bool is_send_complete = false;
  static esp_err_t last_send_result;
  static uint8_t rx_buf[256];
  static uint8_t last_rx_len = 0;
  static char bridge_secret[16];

#ifdef CLIENT_WITHOUT_LORA
  static constexpr uint16_t BRIDGE_PACKET_MAGIC = 0xC03E;
  static const size_t MAX_ESPNOW_PACKET_SIZE = 250;
  /**
   * @brief Common field sizes used by bridge implementations
   *
   * These constants define the size of common packet fields used across bridges.
   * BRIDGE_MAGIC_SIZE is used by all bridges for packet identification.
   * BRIDGE_LENGTH_SIZE is used by bridges that need explicit length fields (like RS232).
   * BRIDGE_CHECKSUM_SIZE is used by all bridges for Fletcher-16 checksums.
   */
  static constexpr uint16_t BRIDGE_MAGIC_SIZE = sizeof(BRIDGE_PACKET_MAGIC);
  static constexpr uint16_t BRIDGE_LENGTH_SIZE = sizeof(uint16_t);
  static constexpr uint16_t BRIDGE_CHECKSUM_SIZE = sizeof(uint16_t);

  static void xorCrypt(uint8_t *data, size_t len) {
    size_t keyLen = strlen(bridge_secret);
    for (size_t i = 0; i < len; i++) {
      data[i] ^= bridge_secret[i % keyLen];
    }
  }

  static uint16_t fletcher16(const uint8_t *data, size_t len) {
    uint8_t sum1 = 0, sum2 = 0;

    for (size_t i = 0; i < len; i++) {
      sum1 = (sum1 + data[i]) % 255;
      sum2 = (sum2 + sum1) % 255;
    }

    return (sum2 << 8) | sum1;
  }

  static bool validateChecksum(const uint8_t *data, size_t len, uint16_t received_checksum) {
    uint16_t calculated_checksum = fletcher16(data, len);
    return received_checksum == calculated_checksum;
  }

#endif

// callback when data is sent
static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  is_send_complete = true;
  ESPNOW_DEBUG_PRINTLN("Send Status: %d", (int)status);
}

static void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  ESPNOW_DEBUG_PRINTLN("Recv: len = %d", len);

  #ifdef CLIENT_WITHOUT_LORA
    if (len < 4) return;

    uint16_t magic = (data[0] << 8) | data[1];
    if (magic != BRIDGE_PACKET_MAGIC) return;

    uint8_t decrypted[MAX_ESPNOW_PACKET_SIZE];
    int encLen = len - 2; // everything except magic
    memcpy(decrypted, data + 2, encLen);
    xorCrypt(decrypted, encLen);

    uint16_t rx_cksum = (decrypted[0] << 8) | decrypted[1];
    uint8_t* meshPayload = decrypted + 2;
    int meshLen = encLen - 2;

    if (!validateChecksum(meshPayload, meshLen, rx_cksum)) return;

    memcpy(rx_buf, meshPayload, meshLen);
    last_rx_len = meshLen;
  #else
    memcpy(rx_buf, data, len);
    last_rx_len = len;
  #endif  
}

void ESPNOWRadio::init() {
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  #ifdef CLIENT_WITHOUT_LORA
    strncpy(bridge_secret, "LVSITANOS", sizeof(bridge_secret));
  #endif
  // Long Range mode
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    ESPNOW_DEBUG_PRINTLN("Error initializing ESP-NOW");
    return;
  }

  esp_wifi_set_max_tx_power(80);  // should be 20dBm

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  is_send_complete = true;

  // Add peer        
  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    ESPNOW_DEBUG_PRINTLN("init success");
  } else {
   // ESPNOW_DEBUG_PRINTLN("Failed to add peer");
  }
}

uint32_t ESPNOWRadio::getRngSeed() {
  return millis() + intID();  // TODO: where to get some entropy?
}

void ESPNOWRadio::setTxPower(uint8_t dbm) {
  esp_wifi_set_max_tx_power(dbm * 4);
}

uint32_t ESPNOWRadio::intID() {
  uint8_t mac[8];
  memset(mac, 0, sizeof(mac));
  esp_efuse_mac_get_default(mac);
  uint32_t n, m;
  memcpy(&n, &mac[0], 4);
  memcpy(&m, &mac[4], 4);
  
  return n + m;
}

bool ESPNOWRadio::startSendRaw(const uint8_t* bytes, int len) {
  // Send message via ESP-NOW
  #ifdef CLIENT_WITHOUT_LORA
    if (!bytes || len <= 0) {
      ESPNOW_DEBUG_PRINTLN("Invalid raw mesh packet");
      return false;
    }

    if (len > (MAX_ESPNOW_PACKET_SIZE - 4)) {
      ESPNOW_DEBUG_PRINTLN("Mesh packet too large for ESP-NOW");
      return false;
    }

    uint8_t out[MAX_ESPNOW_PACKET_SIZE];
    out[0] = (BRIDGE_PACKET_MAGIC >> 8) & 0xFF;
    out[1] = BRIDGE_PACKET_MAGIC & 0xFF;

    uint16_t cksum = fletcher16(bytes, len);
    out[2] = (cksum >> 8) & 0xFF;
    out[3] = cksum & 0xFF;

    memcpy(out + 4, bytes, len);
    xorCrypt(out + 2, len + 2);
    esp_err_t result = esp_now_send(broadcastAddress, out, len + 4);    
  #else
    is_send_complete = false;
    esp_err_t result = esp_now_send(broadcastAddress, bytes, len);
  #endif

  if (result == ESP_OK) {
    n_sent++;
    ESPNOW_DEBUG_PRINTLN("Send success");
    return true;
  }

  last_send_result = result;
  is_send_complete = true;
  ESPNOW_DEBUG_PRINTLN("Send failed: %d", result);
  return false;
}

bool ESPNOWRadio::isSendComplete() {
  return is_send_complete;
}
void ESPNOWRadio::onSendFinished() {
  is_send_complete = true;
}

bool ESPNOWRadio::isInRecvMode() const {
  return is_send_complete;    // if NO send in progress, then we're in Rx mode
}

float ESPNOWRadio::getLastRSSI() const { return 0; }
float ESPNOWRadio::getLastSNR() const { return 0; }

int ESPNOWRadio::recvRaw(uint8_t* bytes, int sz) {
  int len = last_rx_len;
  if (last_rx_len > 0) {
    memcpy(bytes, rx_buf, last_rx_len);
    last_rx_len = 0;
    n_recv++;
  }
  return len;
}

uint32_t ESPNOWRadio::getEstAirtimeFor(int len_bytes) {
  return 4;  // Fast AF
}
