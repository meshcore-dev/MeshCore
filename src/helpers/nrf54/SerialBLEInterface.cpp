#include "SerialBLEInterface.h"
#include <stdio.h>
#include <string.h>
#include "ble_gap.h"
// NOTE: no <ble_hci.h> and no SoftDevice (sd_*) calls, this core's Bluefruit52Lib
// reimplements only the C++ API. SoftDevice GAP calls are swapped for core methods.

#define BLE_HEALTH_CHECK_INTERVAL  10000
#define BLE_RETRY_THROTTLE_MS      250

// Connection parameters (units: interval=1.25ms, timeout=10ms)
#define BLE_MIN_CONN_INTERVAL      12     // 15ms
#define BLE_MAX_CONN_INTERVAL      24     // 30ms

// Advertising parameters (units: 0.625ms)
#define BLE_ADV_INTERVAL_MIN       32     // 20ms
#define BLE_ADV_INTERVAL_MAX       244    // 152.5ms
#define BLE_ADV_FAST_TIMEOUT       30     // seconds

#define BLE_RX_DRAIN_BUF_SIZE      32

static SerialBLEInterface* instance = nullptr;

void SerialBLEInterface::onConnect(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: connected handle=0x%04X", connection_handle);
  if (instance) {
    instance->_conn_handle = connection_handle;
    // Request 2M PHY immediately. On 1M the core's tight T_IFS budget blows during the
    // pairing handshake (EVT_RX/TX_TIMEOUT -> dropped SMP PDUs -> status=8); 2M halves the
    // packet airtime and gives timing margin. Mirrors the Adafruit Bluefruit LE app, which
    // negotiates 2M early and pairs reliably on this core.
    BLEConnection* conn = Bluefruit.Connection(connection_handle);
    if (conn) conn->requestPHY(BLE_GAP_PHY_2MBPS);
#if defined(BLE_NO_PAIRING)
    instance->_isDeviceConnected = true;   // open mode: no securing step, ready immediately
#else
    instance->_isDeviceConnected = false;  // wait for onSecured()
#endif
    instance->clearBuffers();
  }
}

void SerialBLEInterface::onDisconnect(uint16_t connection_handle, uint8_t reason) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disconnected handle=0x%04X reason=%u", connection_handle, reason);
  if (instance && instance->_conn_handle == connection_handle) {
    instance->_conn_handle = BLE_CONN_HANDLE_INVALID;
    instance->_isDeviceConnected = false;
    instance->clearBuffers();
  }
}

void SerialBLEInterface::onSecured(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: onSecured handle=0x%04X", connection_handle);
  if (instance && instance->isValidConnection(connection_handle, true)) {
    instance->_isDeviceConnected = true;
    // Preferred connection interval is set in begin() via Bluefruit.Periph.setConnInterval();
    // the core's Bluefruit52Lib has no SoftDevice conn_param_update on an active link.
  }
}

bool SerialBLEInterface::onPairingPasskey(uint16_t connection_handle, uint8_t const passkey[6], bool match_request) {
  (void)connection_handle; (void)passkey;
  BLE_DEBUG_PRINTLN("SerialBLEInterface: pairing passkey request match=%d", match_request);
  return true;
}

void SerialBLEInterface::onPairingComplete(uint16_t connection_handle, uint8_t auth_status) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: pairing complete handle=0x%04X status=%u", connection_handle, auth_status);
  if (instance && instance->isValidConnection(connection_handle)) {
    if (auth_status != BLE_GAP_SEC_STATUS_SUCCESS) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface: pairing failed, disconnecting");
      instance->disconnect();
    }
  }
}

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  instance = this;

  char charpin[20];
  snprintf(charpin, sizeof(charpin), "%lu", (unsigned long)pin_code);

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();

  char dev_name[32+16];
  if (strcmp(name, "@@MAC") == 0) {
    uint8_t mac[6];
    Bluefruit.getAddr(mac);                 // core API (was sd_ble_gap_addr_get)
    sprintf(name, "%02X%02X%02X%02X%02X%02X",
        mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  }
  sprintf(dev_name, "%s%s", prefix, name);

  // preferred connection interval (was sd_ble_gap_ppcp_set)
  Bluefruit.Periph.setConnInterval(BLE_MIN_CONN_INTERVAL, BLE_MAX_CONN_INTERVAL);

  Bluefruit.setTxPower(BLE_TX_POWER);
  Bluefruit.setName(dev_name);

  (void)charpin;
#if defined(BLE_NO_PAIRING)
  // This core's SMP pairing is unreliable on nRF54 (MITM fails status=11, Just Works fails
  // status=3, never reaches onSecured). Fall back to an OPEN, unencrypted NUS link: no
  // bonding, characteristics readable/writable without pairing. (No MeshCore-level secrecy
  // over the BLE hop, acceptable for bring-up; revisit if the core's SMP is fixed.)
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  bleuart.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  bleuart.begin();
  bleuart.setRxCallback(onBleUartRX);

  bledfu.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  bledfu.begin();
#else
  // Encrypted PIN pairing, matching the core's working Security/pairing_pin example exactly:
  // setPIN + SECMODE_ENC_WITH_MITM, and crucially NO setIOCaps and NO setPairPasskeyCallback.
  // Adding either of those switched the device into a conflicting passkey/numeric-comparison
  // mode and pairing failed (status=11). The central is prompted to enter the 6-digit PIN.
  Bluefruit.Security.setPIN(charpin);
  Bluefruit.Security.setPairCompleteCallback(onPairingComplete);

  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);
  Bluefruit.Security.setSecuredCallback(onSecured);
  // (no Bluefruit.setEventCallback: the raw conn-param-update event handler is nRF52/
  //  SoftDevice-only and not needed, defaults negotiate fine.)

  bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  bleuart.begin();
  bleuart.setRxCallback(onBleUartRX);

  bledfu.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  bledfu.begin();
#endif

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.setInterval(BLE_ADV_INTERVAL_MIN, BLE_ADV_INTERVAL_MAX);
  Bluefruit.Advertising.setFastTimeout(BLE_ADV_FAST_TIMEOUT);
  Bluefruit.Advertising.restartOnDisconnect(true);
}

void SerialBLEInterface::clearBuffers() {
  send_queue_len = 0;
  recv_queue_len = 0;
  _last_retry_attempt = 0;
  bleuart.flush();
}

void SerialBLEInterface::shiftSendQueueLeft() {
  if (send_queue_len > 0) {
    send_queue_len--;
    for (uint8_t i = 0; i < send_queue_len; i++) send_queue[i] = send_queue[i + 1];
  }
}

void SerialBLEInterface::shiftRecvQueueLeft() {
  if (recv_queue_len > 0) {
    recv_queue_len--;
    for (uint8_t i = 0; i < recv_queue_len; i++) recv_queue[i] = recv_queue[i + 1];
  }
}

bool SerialBLEInterface::isValidConnection(uint16_t handle, bool requireWaitingForSecurity) const {
  if (_conn_handle != handle) return false;
  BLEConnection* conn = Bluefruit.Connection(handle);
  if (conn == nullptr || !conn->connected()) return false;
  if (requireWaitingForSecurity && _isDeviceConnected) return false;
  return true;
}

bool SerialBLEInterface::isAdvertising() const {
  // The core's Bluefruit52Lib has no advertising-running query; restartOnDisconnect(true)
  // keeps advertising alive, so report true (the watchdog restart below is a no-op).
  return true;
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  clearBuffers();
  _last_health_check = millis();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);
}

void SerialBLEInterface::disconnect() {
  if (_conn_handle != BLE_CONN_HANDLE_INVALID) {
    Bluefruit.disconnect(_conn_handle);     // core API (was sd_ble_gap_disconnect)
  }
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disable");
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
  disconnect();
  _last_health_check = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%u", (unsigned)len);
    return 0;
  }
  if (isConnected() && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }
    send_queue[send_queue_len].len = len;
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;
    return len;
  }
  return 0;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (send_queue_len > 0) {
    if (!isConnected()) {
      send_queue_len = 0;
    } else {
      unsigned long now = millis();
      bool throttle_active = (_last_retry_attempt > 0 && (now - _last_retry_attempt) < BLE_RETRY_THROTTLE_MS);
      if (!throttle_active) {
        Frame frame_to_send = send_queue[0];
        size_t written = bleuart.write(frame_to_send.buf, frame_to_send.len);
        if (written == frame_to_send.len) {
          _last_retry_attempt = 0;
          shiftSendQueueLeft();
        } else if (written > 0) {
          _last_retry_attempt = 0;
          shiftSendQueueLeft();
        } else {
          if (!isConnected()) { _last_retry_attempt = 0; shiftSendQueueLeft(); }
          else { _last_retry_attempt = now; }
        }
      }
    }
  }

  if (recv_queue_len > 0) {
    size_t len = recv_queue[0].len;
    memcpy(dest, recv_queue[0].buf, len);
    shiftRecvQueueLeft();
    return len;
  }

  unsigned long now = millis();
  if (_isEnabled && !isConnected() && _conn_handle == BLE_CONN_HANDLE_INVALID) {
    if (now - _last_health_check >= BLE_HEALTH_CHECK_INTERVAL) {
      _last_health_check = now;
      if (!isAdvertising()) Bluefruit.Advertising.start(0);
    }
  }
  return 0;
}

void SerialBLEInterface::onBleUartRX(uint16_t conn_handle) {
  if (!instance) return;
  if (instance->_conn_handle != conn_handle || !instance->isConnected()) {
    while (instance->bleuart.available() > 0) instance->bleuart.read();
    return;
  }
  while (instance->bleuart.available() > 0) {
    if (instance->recv_queue_len >= FRAME_QUEUE_SIZE) {
      while (instance->bleuart.available() > 0) instance->bleuart.read();
      break;
    }
    int avail = instance->bleuart.available();
    if (avail > MAX_FRAME_SIZE) {
      uint8_t drain_buf[BLE_RX_DRAIN_BUF_SIZE];
      while (instance->bleuart.available() > 0) {
        int chunk = instance->bleuart.available() > BLE_RX_DRAIN_BUF_SIZE ? BLE_RX_DRAIN_BUF_SIZE : instance->bleuart.available();
        instance->bleuart.readBytes(drain_buf, chunk);
      }
      continue;
    }
    int read_len = avail;
    instance->recv_queue[instance->recv_queue_len].len = read_len;
    instance->bleuart.readBytes(instance->recv_queue[instance->recv_queue_len].buf, read_len);
    instance->recv_queue_len++;
  }
}

bool SerialBLEInterface::isConnected() const {
  return _isDeviceConnected && Bluefruit.connected() > 0;
}

bool SerialBLEInterface::isWriteBusy() const {
  return send_queue_len >= (FRAME_QUEUE_SIZE * 2 / 3);
}
