#include "ESPNOWRadio.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

static uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static esp_now_peer_info_t peerInfo;
static volatile bool is_send_complete = false;
static esp_err_t last_send_result;
static ESPNOWRadio* espnow_instance = nullptr;

// callback when data is sent
static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;
  is_send_complete = true;
  ESPNOW_DEBUG_PRINTLN("Send Status: %d", (int)status);
}

static void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  (void)mac;
  ESPNOW_DEBUG_PRINTLN("Recv: len = %d", len);
  if (!espnow_instance || len <= 0 || len > 256) {
    return;
  }
  espnow_instance->enqueueRxFrame(data, len);
}

void ESPNOWRadio::init() {
  espnow_instance = this;
  _rx_read_idx = 0;
  _rx_write_idx = 0;
  _rx_count = 0;
  _rx_drop_count = 0;

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
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
  is_send_complete = false;
  esp_err_t result = esp_now_send(broadcastAddress, bytes, len);
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
  PendingFrame frame;
  if (popPendingFrame(frame)) {
    int len = frame.len;
    if (len > sz) {
      len = sz;
    }
    memcpy(bytes, frame.buf, len);
    n_recv++;
    return len;
  }
  return 0;
}

bool ESPNOWRadio::enqueueRxFrame(const uint8_t* data, int len) {
  portENTER_CRITICAL(&_rx_lock);
  if (_rx_count >= RX_QUEUE_SIZE) {
    _rx_drop_count++;
    portEXIT_CRITICAL(&_rx_lock);
    ESPNOW_DEBUG_PRINTLN("Recv queue full, dropping frame len=%d (drops=%lu)", len,
                         (unsigned long)_rx_drop_count);
    return false;
  }

  PendingFrame& slot = _rx_queue[_rx_write_idx];
  slot.len = len;
  memcpy(slot.buf, data, len);
  _rx_write_idx = (_rx_write_idx + 1) % RX_QUEUE_SIZE;
  _rx_count++;
  portEXIT_CRITICAL(&_rx_lock);
  return true;
}

bool ESPNOWRadio::popPendingFrame(PendingFrame& frame) {
  bool have_frame = false;
  portENTER_CRITICAL(&_rx_lock);
  if (_rx_count > 0) {
    frame = _rx_queue[_rx_read_idx];
    _rx_read_idx = (_rx_read_idx + 1) % RX_QUEUE_SIZE;
    _rx_count--;
    have_frame = true;
  }
  portEXIT_CRITICAL(&_rx_lock);
  return have_frame;
}

uint32_t ESPNOWRadio::getEstAirtimeFor(int len_bytes) {
  return 4;  // Fast AF
}
