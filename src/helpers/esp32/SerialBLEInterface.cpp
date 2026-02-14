#include "SerialBLEInterface.h"
#include <string.h>
#include <esp_gap_ble_api.h>

// ESP32 TX Model: Bluedroid's notify() has no async TX complete event for
// notifications (only indications get ESP_GATTS_CONF_EVT). The onStatus()
// callback fires synchronously within notify(), detecting immediate errors
// but not when transmission actually completes. This polling model with
// rate limiting is optimal for this constraint. See NRF52 implementation
// for event-driven TX using BLE_GATTS_EVT_HVN_TX_COMPLETE.

SerialBLEInterface* SerialBLEInterface::instance = nullptr;

void SerialBLEInterface::gapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  if (!instance) return;

  switch (event) {
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
      instance->onConnParamsUpdate(param);
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      instance->onAdvStartComplete(param);
      break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
      instance->onAdvStopComplete(param);
      break;
    default:
      break;
  }
}

void SerialBLEInterface::onConnParamsUpdate(esp_ble_gap_cb_param_t* param) {
  if (param->update_conn_params.status != ESP_BT_STATUS_SUCCESS) {
    BLE_DEBUG_PRINTLN("Failed to request connection parameter update: %d", param->update_conn_params.status);
    _conn_param_update_pending = false;
    return;
  }

  // Check if this update matches our peer address
  if (memcmp(param->update_conn_params.bda, _peer_addr, sizeof(esp_bd_addr_t)) != 0) {
    return;
  }

  uint16_t interval = param->update_conn_params.conn_int;  // Actual connection interval applied
  uint16_t latency = param->update_conn_params.latency;
  uint16_t timeout = param->update_conn_params.timeout;

  BLE_DEBUG_PRINTLN("CONN_PARAM_UPDATE: interval=%u, latency=%u, timeout=%u",
                   interval, latency, timeout);

  // Check if sync mode parameters were applied (matching nRF52 logic)
  if (latency == BLE_SYNC_SLAVE_LATENCY &&
      timeout == BLE_SYNC_CONN_SUP_TIMEOUT &&
      interval >= BLE_SYNC_MIN_CONN_INTERVAL &&
      interval <= BLE_SYNC_MAX_CONN_INTERVAL) {
    if (!_sync_mode) {
      BLE_DEBUG_PRINTLN("Sync mode confirmed by connection parameters");
      _sync_mode = true;
      _last_activity_time = millis();
    }
  } else if (latency == BLE_SLAVE_LATENCY &&
             timeout == BLE_CONN_SUP_TIMEOUT &&
             interval >= BLE_MIN_CONN_INTERVAL &&
             interval <= BLE_MAX_CONN_INTERVAL) {
    if (_sync_mode) {
      BLE_DEBUG_PRINTLN("Default mode confirmed by connection parameters");
      _sync_mode = false;
    }
  }

  _conn_param_update_pending = false;
}

void SerialBLEInterface::onAdvStartComplete(esp_ble_gap_cb_param_t* param) {
  if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
    _isAdvertising = true;
    BLE_DEBUG_PRINTLN("SerialBLEInterface: advertising started (GAP event confirmed)");
  } else {
    _isAdvertising = false;
    BLE_DEBUG_PRINTLN("SerialBLEInterface: advertising start failed, status=%d", param->adv_start_cmpl.status);
  }
}

void SerialBLEInterface::onAdvStopComplete(esp_ble_gap_cb_param_t* param) {
  if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS || param->adv_stop_cmpl.status == ESP_BT_STATUS_UNSUPPORTED) {
    _isAdvertising = false;
    BLE_DEBUG_PRINTLN("SerialBLEInterface: advertising stopped (GAP event confirmed)");
  }
}

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  instance = this;
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", prefix, name);

  // Create the BLE Device
  BLEDevice::init(dev_name);
  BLEDevice::setSecurityCallbacks(this);
  BLEDevice::setMTU(BLE_MAX_MTU);

  BLESecurity sec;
  sec.setStaticPIN(pin_code);
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  if (!pServer) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create BLE server");
    return;
  }
  pServer->setCallbacks(this);

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);
  if (!pService) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create BLE service");
    return;
  }

  // Create TX Characteristic (notify to client)
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  if (!pTxCharacteristic) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create TX characteristic");
    return;
  }
  pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  pTxCharacteristic->addDescriptor(new BLE2902());

  // Create RX Characteristic (write from client)
  // Support both WRITE (with response) and WRITE_NR (without response) for flexibility
  BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  if (!pRxCharacteristic) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create RX characteristic");
    return;
  }
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  pRxCharacteristic->setCallbacks(this);

  // Configure advertising
  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setMinInterval(BLE_ADV_INTERVAL_MIN);
  pAdvertising->setMaxInterval(BLE_ADV_INTERVAL_MAX);
  pAdvertising->setScanResponse(true);

  // Register custom GAP handler to receive connection parameter update events
  BLEDevice::setCustomGapHandler(gapEventHandler);
}

// -------- BLESecurityCallbacks methods

uint32_t SerialBLEInterface::onPassKeyRequest() {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: passkey request");
  return _pin_code;
}

void SerialBLEInterface::onPassKeyNotify(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: passkey notify: %u", pass_key);
}

bool SerialBLEInterface::onConfirmPIN(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: confirm PIN: %u", pass_key);
  return true;
}

bool SerialBLEInterface::onSecurityRequest() {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: security request");
  return true;
}

void SerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
  if (_conn_handle == BLE_CONN_HANDLE_INVALID) {
    // Windows can deliver auth complete before onConnect gives us conn_id.
    BLE_DEBUG_PRINTLN("onAuthenticationComplete: deferring result until onConnect");
    _authPending = true;
    _authPendingSuccess = cmpl.success;
    memcpy(_authPendingAddr, cmpl.bd_addr, sizeof(_authPendingAddr));
    return;
  }

  if (!isValidConnection(_conn_handle, true)) {
    BLE_DEBUG_PRINTLN("onAuthenticationComplete: ignoring stale/duplicate callback");
    return;
  }

  if (cmpl.success) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: authentication successful");
    _isDeviceConnected = true;

    // We've just connected, there will be data sync, so enable sync mode immediately
    _sync_mode = true;
    _last_activity_time = millis();
    _conn_param_update_pending = true;

    // Request sync mode connection parameters
    if (pServer) {
      pServer->updateConnParams(_peer_addr,
                                BLE_SYNC_MIN_CONN_INTERVAL,
                                BLE_SYNC_MAX_CONN_INTERVAL,
                                BLE_SYNC_SLAVE_LATENCY,
                                BLE_SYNC_CONN_SUP_TIMEOUT);
      BLE_DEBUG_PRINTLN(
        "Sync mode requested on secure: %u-%ums interval, latency=%u, %ums timeout",
        BLE_SYNC_MIN_CONN_INTERVAL * 5 / 4,
        BLE_SYNC_MAX_CONN_INTERVAL * 5 / 4,
        BLE_SYNC_SLAVE_LATENCY,
        BLE_SYNC_CONN_SUP_TIMEOUT * 10
      );
      // We now have a custom GAP handler that will receive ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT
      // The pending flag will be cleared by onConnParamsUpdate callback when update completes
      // Keep _conn_param_update_pending = true until callback confirms

      // Request Data Length Extension (DLE) - ESP needs this set manually, nRF52 handles automatically
      // Bluedroid API only sets tx_data_length (octets), time component is auto-negotiated
      esp_err_t err_code = esp_ble_gap_set_pkt_data_len(_peer_addr, BLE_DLE_MAX_TX_OCTETS);
      if (err_code == ESP_OK) {
        BLE_DEBUG_PRINTLN("Data Length Extension requested: max_tx_octets=%u", BLE_DLE_MAX_TX_OCTETS);
      } else {
        BLE_DEBUG_PRINTLN("Failed to request Data Length Extension: %d", err_code);
      }
    }
  } else {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: authentication failed, disconnecting");
    if (pServer && _conn_handle != BLE_CONN_HANDLE_INVALID) {
      pServer->disconnect(_conn_handle);
    }
    _last_health_check = millis();
  }
}

// -------- BLEServerCallbacks methods

void SerialBLEInterface::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: connected conn_id=%d", param->connect.conn_id);

  // Reject additional connections (single connection only)
  if (pServer->getConnectedCount() > 1) {
    pServer->disconnect(param->connect.conn_id);
    BLE_DEBUG_PRINTLN("SerialBLEInterface: rejecting second connection, already have %d connection", pServer->getConnectedCount() - 1);
    return;
  }

  _conn_handle = param->connect.conn_id;
  memcpy(_peer_addr, param->connect.remote_bda, sizeof(esp_bd_addr_t));
  _sync_mode = false;
  _conn_param_update_pending = false;
  _isDeviceConnected = false;  // Wait for authentication
  _isAdvertising = false;  // Advertising stops automatically when connection is established
  clearBuffers();

  if (_authPending && memcmp(_authPendingAddr, _peer_addr, sizeof(_peer_addr)) == 0) {
    _authPending = false;
    if (_authPendingSuccess) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface: applying deferred auth result");
      _isDeviceConnected = true;
      _sync_mode = true;
      _last_activity_time = millis();
      _conn_param_update_pending = true;

      if (pServer) {
        pServer->updateConnParams(_peer_addr,
                                  BLE_SYNC_MIN_CONN_INTERVAL,
                                  BLE_SYNC_MAX_CONN_INTERVAL,
                                  BLE_SYNC_SLAVE_LATENCY,
                                  BLE_SYNC_CONN_SUP_TIMEOUT);
        BLE_DEBUG_PRINTLN(
          "Sync mode requested on secure: %u-%ums interval, latency=%u, %ums timeout",
          BLE_SYNC_MIN_CONN_INTERVAL * 5 / 4,
          BLE_SYNC_MAX_CONN_INTERVAL * 5 / 4,
          BLE_SYNC_SLAVE_LATENCY,
          BLE_SYNC_CONN_SUP_TIMEOUT * 10
        );

        esp_err_t err_code = esp_ble_gap_set_pkt_data_len(_peer_addr, BLE_DLE_MAX_TX_OCTETS);
        if (err_code == ESP_OK) {
          BLE_DEBUG_PRINTLN("Data Length Extension requested: max_tx_octets=%u", BLE_DLE_MAX_TX_OCTETS);
        } else {
          BLE_DEBUG_PRINTLN("Failed to request Data Length Extension: %d", err_code);
        }
      }
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface: deferred auth failed, disconnecting");
      if (pServer && _conn_handle != BLE_CONN_HANDLE_INVALID) {
        pServer->disconnect(_conn_handle);
      }
      _last_health_check = millis();
    }
  } else if (_authPending) {
    // Different peer connected; drop stale pending state.
    _authPending = false;
  }
}

void SerialBLEInterface::onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
#if BLE_DEBUG_LOGGING
  const char* initiator;
  uint8_t reason = param->disconnect.reason;
  if (reason == 0x16) {
    initiator = "local";
  } else if (reason == 0x08) {
    initiator = "timeout";
  } else {
    initiator = "remote";
  }
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disconnected conn_handle=%d reason=0x%02X (initiated by %s)", 
                    param->disconnect.conn_id, reason, initiator);
#endif

  if (_conn_handle == param->disconnect.conn_id) {
    _conn_handle = BLE_CONN_HANDLE_INVALID;
    _sync_mode = false;
    _conn_param_update_pending = false;
    _isDeviceConnected = false;
    _authPending = false;
    memset(_peer_addr, 0, sizeof(_peer_addr));
    clearBuffers();
    _last_health_check = millis();

    if (_isEnabled) {
      BLEAdvertising* pAdvertising = pServer->getAdvertising();
      if (pAdvertising) {
        pAdvertising->start();
        // State will be updated by ESP_GAP_BLE_ADV_START_COMPLETE_EVT event
        // Optimistically set flag - GAP event will confirm
        _isAdvertising = true;
        BLE_DEBUG_PRINTLN("SerialBLEInterface: restarting advertising on disconnect");
      }
    }
  }
}

// -------- BLECharacteristicCallbacks methods

void SerialBLEInterface::onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
  if (!isConnected()) {
    return;
  }

  // Check connection handle matches (like NimBLE test)
  if (param->write.conn_id != _conn_handle) {
    BLE_DEBUG_PRINTLN("onWrite: ignoring write from stale connection handle %d (expected %d)", 
                     param->write.conn_id, _conn_handle);
    return;
  }

  uint8_t* rxValue = pCharacteristic->getData();
  size_t len = pCharacteristic->getLength();

  BLE_DEBUG_PRINTLN("onWrite: len=%u, queue=%u", (unsigned)len, (unsigned)recv_queue.size());

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("onWrite: frame too big, len=%u", (unsigned)len);
    return;
  }

  if (recv_queue.isFull()) {
    BLE_DEBUG_PRINTLN("onWrite: recv queue full, dropping data");
    return;
  }

  if (rxValue == nullptr && len > 0) {
    BLE_DEBUG_PRINTLN("onWrite: invalid data pointer");
    return;
  }

  SerialBLEFrame* frame = recv_queue.getWriteSlot();
  if (frame) {
    frame->len = len;
    memcpy(frame->buf, rxValue, len);
    recv_queue.push();
  }

  unsigned long now = millis();
  if (noteFrameActivity(now, len)) {
    requestSyncModeConnection();
  }
}

void SerialBLEInterface::onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
  // Handle notification/indication status (called synchronously by BLECharacteristic::notify())
  // For notifications, onStatus is called immediately with SUCCESS_NOTIFY or error codes
  // This allows us to detect immediate failures from esp_ble_gatts_send_indicate()
  // Unlike nRF52's synchronous write() which returns bytes written, Bluedroid's notify() is async
  // but calls onStatus synchronously, so we can detect immediate API errors
  if (pCharacteristic == pTxCharacteristic) {
    if (s == ERROR_GATT || s == ERROR_NO_CLIENT || s == ERROR_NOTIFY_DISABLED) {
      // Set flag so checkRecvFrame can detect failure after notify() returns
      _notify_failed = true;
      BLE_DEBUG_PRINTLN("onStatus: notify failed, status=%d, code=%lu", s, (unsigned long)code);
    }
    // For SUCCESS_NOTIFY, _notify_failed remains false, frame will be popped in checkRecvFrame
  }
}

// ---------- Helper methods

void SerialBLEInterface::clearBuffers() {
  clearTransferState();
}

bool SerialBLEInterface::isValidConnection(uint16_t conn_handle, bool requireWaitingForSecurity) const {
  if (_conn_handle != conn_handle) {
    return false;
  }
  if (_conn_handle == BLE_CONN_HANDLE_INVALID) {
    return false;
  }
  if (requireWaitingForSecurity && _isDeviceConnected) {
    return false;
  }
  return true;
}

bool SerialBLEInterface::isAdvertising() const {
  // Track advertising state via GAP events (ESP_GAP_BLE_ADV_START_COMPLETE_EVT / ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT)
  // This is more reliable than checking object existence - it reflects actual BLE stack state
  return _isAdvertising;
}

void SerialBLEInterface::requestSyncModeConnection() {
  if (!pServer || !isConnected()) {
    return;
  }

  if (_sync_mode) {
    return;
  }

  if (_conn_param_update_pending) {
    return;
  }
  _conn_param_update_pending = true;

  BLE_DEBUG_PRINTLN("Requesting sync mode connection: %u-%ums interval, latency=%u, %ums timeout",
                    BLE_SYNC_MIN_CONN_INTERVAL * 5 / 4,
                    BLE_SYNC_MAX_CONN_INTERVAL * 5 / 4,
                    BLE_SYNC_SLAVE_LATENCY,
                    BLE_SYNC_CONN_SUP_TIMEOUT * 10);

  pServer->updateConnParams(_peer_addr,
                            BLE_SYNC_MIN_CONN_INTERVAL,
                            BLE_SYNC_MAX_CONN_INTERVAL,
                            BLE_SYNC_SLAVE_LATENCY,
                            BLE_SYNC_CONN_SUP_TIMEOUT);

  // We now have a custom GAP handler that will receive ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT
  // Unlike nRF52 which can detect NRF_ERROR_BUSY and retry, Bluedroid's updateConnParams is void
  // We can't detect errors immediately, but we can detect success via callback
  // Don't set _sync_mode here - wait for callback to confirm (matching nRF52 behavior)
  // Keep _conn_param_update_pending = true until callback confirms or times out
}

void SerialBLEInterface::requestDefaultConnection() {
  if (!pServer || !isConnected()) {
    return;
  }

  if (!_sync_mode) {
    return;
  }

  if (!send_queue.isEmpty() || !recv_queue.isEmpty()) {
    return;
  }

  if (_conn_param_update_pending) {
    return;
  }
  _conn_param_update_pending = true;

  BLE_DEBUG_PRINTLN("Requesting default connection: %u-%ums interval, latency=%u, %ums timeout",
                    BLE_MIN_CONN_INTERVAL * 5 / 4,
                    BLE_MAX_CONN_INTERVAL * 5 / 4,
                    BLE_SLAVE_LATENCY,
                    BLE_CONN_SUP_TIMEOUT * 10);

  pServer->updateConnParams(_peer_addr,
                            BLE_MIN_CONN_INTERVAL,
                            BLE_MAX_CONN_INTERVAL,
                            BLE_SLAVE_LATENCY,
                            BLE_CONN_SUP_TIMEOUT);

  // We now have a custom GAP handler that will receive ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT
  // Unlike nRF52 which can detect NRF_ERROR_BUSY and retry, Bluedroid's updateConnParams is void
  // We can't detect errors immediately, but we can detect success via callback
  // Don't set _sync_mode here - wait for callback to confirm (matching nRF52 behavior)
  // Keep _conn_param_update_pending = true until callback confirms or times out
}

// ---------- Public methods

void SerialBLEInterface::enable() {
  if (_isEnabled) return;

  if (!pServer) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: enable() failed - pServer is null");
    return;
  }

  _isEnabled = true;
  clearBuffers();
  _last_health_check = millis();

  // Start the service
  pService->start();

  // Start advertising
  // State will be updated by ESP_GAP_BLE_ADV_START_COMPLETE_EVT event
  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  if (pAdvertising) {
    pAdvertising->start();
    // Optimistically set flag - GAP event will confirm
    _isAdvertising = true;
    BLE_DEBUG_PRINTLN("SerialBLEInterface: enable() - advertising started");
  }
}

void SerialBLEInterface::disconnect() {
  if (_conn_handle != BLE_CONN_HANDLE_INVALID && pServer) {
    pServer->disconnect(_conn_handle);
  }
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disable");

  disconnect();
  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  if (pAdvertising) {
    pAdvertising->stop();
    // State will be updated by ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT event
    _isAdvertising = false;
  }
  pService->stop();
  _isDeviceConnected = false;
  memset(_peer_addr, 0, sizeof(_peer_addr));
  _last_health_check = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%u", (unsigned)len);
    return 0;
  }

  bool connected = isConnected();
  if (connected && len > 0) {
    if (send_queue.isFull()) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    SerialBLEFrame* frame = send_queue.getWriteSlot();
    if (frame) {
      frame->len = len;
      memcpy(frame->buf, src, len);
      send_queue.push();
      return len;
    }
  }
  return 0;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  // Process send queue
  if (!send_queue.isEmpty()) {
    if (!isConnected()) {
      BLE_DEBUG_PRINTLN("writeBytes: connection invalid, clearing send queue");
      send_queue.init();
    } else {
      unsigned long now = millis();
      bool throttle_active = (_last_retry_attempt > 0 && (now - _last_retry_attempt) < BLE_RETRY_THROTTLE_MS);
      bool send_interval_ok = (_last_send_time == 0 || (now - _last_send_time) >= BLE_MIN_SEND_INTERVAL_MS);

      if (!throttle_active && send_interval_ok && pTxCharacteristic) {
        SerialBLEFrame* frame_to_send = send_queue.peekFront();
        if (frame_to_send) {
          _notify_failed = false;  // Reset flag before notify
          pTxCharacteristic->setValue(frame_to_send->buf, frame_to_send->len);
          pTxCharacteristic->notify();

          // onStatus() is called synchronously within notify(), so check flag now
          if (_notify_failed) {
            if (!isConnected()) {
              BLE_DEBUG_PRINTLN("writeBytes failed: connection lost, dropping frame");
              _last_retry_attempt = 0;
              _last_send_time = 0;
              popSendQueue();
            } else {
              BLE_DEBUG_PRINTLN("writeBytes failed (buffer full), keeping frame for retry");
              _last_retry_attempt = now;
            }
          } else {
            BLE_DEBUG_PRINTLN("writeBytes: sz=%u, hdr=%u", (unsigned)frame_to_send->len, (unsigned)frame_to_send->buf[0]);
            _last_retry_attempt = 0;
            _last_send_time = now;
            if (noteFrameActivity(now, frame_to_send->len)) {
              requestSyncModeConnection();
            }
            popSendQueue();
          }
        }
      }
    }
  }

  // Process receive queue
  if (!recv_queue.isEmpty()) {
    SerialBLEFrame* frame = recv_queue.peekFront();
    if (frame) {
      size_t len = frame->len;
      memcpy(dest, frame->buf, len);

      BLE_DEBUG_PRINTLN("readBytes: sz=%u, hdr=%u", (unsigned)len, (unsigned)dest[0]);

      popRecvQueue();
      return len;
    }
  }

  // Check for sync mode timeout
  unsigned long now = millis();
  if (isConnected() && _sync_mode && _last_activity_time > 0 &&
      send_queue.isEmpty() && recv_queue.isEmpty()) {
    if (now - _last_activity_time >= BLE_SYNC_INACTIVITY_TIMEOUT_MS) {
      requestDefaultConnection();
    }
  }

  // Advertising watchdog - restart if stopped unexpectedly
  if (_isEnabled && !isConnected() && _conn_handle == BLE_CONN_HANDLE_INVALID) {
    if (now - _last_health_check >= BLE_HEALTH_CHECK_INTERVAL) {
      _last_health_check = now;

      if (!isAdvertising()) {
        BLE_DEBUG_PRINTLN("SerialBLEInterface: advertising watchdog - advertising stopped, restarting");
        BLEAdvertising* pAdvertising = pServer->getAdvertising();
        if (pAdvertising) {
          pAdvertising->start();
          // State will be updated by ESP_GAP_BLE_ADV_START_COMPLETE_EVT event
          // Optimistically set flag - GAP event will confirm
          _isAdvertising = true;
        }
      }
    }
  }

  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return _isDeviceConnected && _conn_handle != BLE_CONN_HANDLE_INVALID && pServer && pServer->getConnectedCount() > 0;
}

bool SerialBLEInterface::isWriteBusy() const {
  return isWriteBusyCommon();
}
