#include "SerialBLEInterface.h"
#include "../SerialBLECommon.h"
#include <stdio.h>
#include <string.h>

void SerialBLEInterface::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: connected conn_handle=%d", connInfo.getConnHandle());
  if (pServer->getConnectedCount() > 1) {
    bool success = pServer->disconnect(connInfo.getConnHandle());
    if (!success) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to disconnect second connection");
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface: rejecting second connection, already have %d connection", pServer->getConnectedCount() - 1);
    }
    return;
  }
  
  _conn_handle = connInfo.getConnHandle();
  _isDeviceConnected = false;
  clearBuffers(); // this seems redundant, but there were edge cases where stuff stuck in the buffers on rapid disconnect-connects
}

void SerialBLEInterface::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
#if BLE_DEBUG_LOGGING
  const char* initiator;
  if (reason == 0x16) {
    initiator = "local";
  } else if (reason == 0x08) {
    initiator = "timeout";
  } else {
    initiator = "remote";
  }
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disconnected conn_handle=%d reason=0x%02X (initiated by %s)", 
                    connInfo.getConnHandle(), reason, initiator);
#endif
  if (_conn_handle == connInfo.getConnHandle()) {
    _conn_handle = BLE_CONN_HANDLE_INVALID;
    _isDeviceConnected = false;
    clearBuffers();
    _last_health_check = millis();
    
    if (_isEnabled) {
      NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
      if (pAdvertising && !pAdvertising->isAdvertising()) {
        pAdvertising->start(0);
        BLE_DEBUG_PRINTLN("SerialBLEInterface: restarting advertising on disconnect");
      }
    }
  }
}

void SerialBLEInterface::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: onAuthenticationComplete conn_handle=%d", connInfo.getConnHandle());
  if (isValidConnection(connInfo.getConnHandle(), true)) {
    if (!connInfo.isAuthenticated()) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface: authentication failed, disconnecting");
      disconnect();
      return;
    }
    
    BLE_DEBUG_PRINTLN("SerialBLEInterface: authentication successful");
    _isDeviceConnected = true;
    
    if (pServer) {
      pServer->updateConnParams(connInfo.getConnHandle(),
                                BLE_MIN_CONN_INTERVAL,
                                BLE_MAX_CONN_INTERVAL,
                                BLE_SLAVE_LATENCY,
                                BLE_CONN_SUP_TIMEOUT);
      BLE_DEBUG_PRINTLN("Connection parameter update requested: %u-%ums interval, latency=%u, %ums timeout",
                       BLE_MIN_CONN_INTERVAL * 5 / 4,
                       BLE_MAX_CONN_INTERVAL * 5 / 4,
                       BLE_SLAVE_LATENCY,
                       BLE_CONN_SUP_TIMEOUT * 10);
      
      extern int ble_gap_set_data_len(uint16_t conn_handle, uint16_t tx_octets, uint16_t tx_time);
      int err_code = ble_gap_set_data_len(connInfo.getConnHandle(), BLE_DLE_MAX_TX_OCTETS, BLE_DLE_MAX_TX_TIME_US);
      if (err_code == 0) {
        BLE_DEBUG_PRINTLN("Data Length Extension requested: max_tx_octets=%u, max_tx_time=%uus",
                         BLE_DLE_MAX_TX_OCTETS, BLE_DLE_MAX_TX_TIME_US);
      } else {
        BLE_DEBUG_PRINTLN("Failed to request Data Length Extension: %d", err_code);
      }
    }
  } else {
    BLE_DEBUG_PRINTLN("onAuthenticationComplete: ignoring stale/duplicate callback");
  }
}

void SerialBLEInterface::onConnParamsUpdate(NimBLEConnInfo& connInfo) {
  uint16_t interval_ms = connInfo.getConnInterval() * 5 / 4;
  uint16_t timeout_ms = connInfo.getConnTimeout() * 10;
  BLE_DEBUG_PRINTLN("SerialBLEInterface: onConnParamsUpdate conn_handle=%d, interval=%ums, latency=%u, timeout=%ums",
                   connInfo.getConnHandle(),
                   interval_ms,
                   connInfo.getConnLatency(),
                   timeout_ms);
  
  if (connInfo.getConnHandle() != _conn_handle) {
    return;
  }
  
  uint16_t interval = connInfo.getConnInterval();
  uint16_t latency = connInfo.getConnLatency();
  uint16_t timeout = connInfo.getConnTimeout();
  
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

void SerialBLEInterface::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  if (!isConnected()) {
    return;
  }
  
  if (connInfo.getConnHandle() != _conn_handle) {
    BLE_DEBUG_PRINTLN("onWrite: ignoring write from stale connection handle %d (expected %d)", 
                     connInfo.getConnHandle(), _conn_handle);
    return;
  }
  
  auto val = pCharacteristic->getValue();
  size_t len = val.length();
  
  BLE_DEBUG_PRINTLN("onWrite: len=%u, queue=%u", (unsigned)len, (unsigned)recv_queue.size());
  
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("onWrite: frame too big, len=%u", (unsigned)len);
    return;
  }
  
  if (recv_queue.isFull()) {
    BLE_DEBUG_PRINTLN("onWrite: recv queue full, dropping data");
    return;
  }
  
  const uint8_t* data = val.data();
  if (data == nullptr && len > 0) {
    BLE_DEBUG_PRINTLN("onWrite: invalid data pointer");
    return;
  }
  
  SerialBLEFrame* frame = recv_queue.getWriteSlot();
  if (frame) {
    frame->len = len;
    memcpy(frame->buf, data, len);
    recv_queue.push();
  }

  unsigned long now = millis();
  if (noteFrameActivity(now, len)) {
    requestSyncModeConnection();
  }
}

void SerialBLEInterface::begin(const char* device_name, uint32_t pin_code) {
  NimBLEDevice::init(device_name);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(pin_code);
  NimBLEDevice::setMTU(BLE_MAX_MTU);

  pServer = NimBLEDevice::createServer();
  if (!pServer) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create BLE server");
    return;
  }
  pServer->setCallbacks(this);

  pService = pServer->createService(SERVICE_UUID);
  if (!pService) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create BLE service");
    return;
  }

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC
  );
  if (!pTxCharacteristic) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create TX characteristic");
    return;
  }

  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC
  );
  if (!pRxCharacteristic) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to create RX characteristic");
    return;
  }
  pRxCharacteristic->setCallbacks(this);

  if (!pService->start()) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to start BLE service");
    return;
  }

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
  pAdvertising->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  pAdvertising->addTxPower();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setMinInterval(BLE_ADV_INTERVAL_MIN);
  pAdvertising->setMaxInterval(BLE_ADV_INTERVAL_MAX);
  pAdvertising->setPreferredParams(BLE_MIN_CONN_INTERVAL, BLE_MAX_CONN_INTERVAL);
  pAdvertising->enableScanResponse(true);
  
  NimBLEAdvertisementData scanRespData;
  scanRespData.setName(device_name);
  pAdvertising->setScanResponseData(scanRespData);
}

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
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  return pAdvertising && pAdvertising->isAdvertising();
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;

  if (!pServer) {
    BLE_DEBUG_PRINTLN("SerialBLEInterface: enable() failed - pServer is null");
    return;
  }

  _isEnabled = true;
  clearBuffers();
  _last_health_check = millis();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  if (pAdvertising) {
    pAdvertising->start(0);
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
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  if (pAdvertising) {
    pAdvertising->stop();
  }
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
          pTxCharacteristic->setValue(frame_to_send->buf, frame_to_send->len);
          bool success = pTxCharacteristic->notify();
          
          if (success) {
            BLE_DEBUG_PRINTLN("writeBytes: sz=%u, hdr=%u", (unsigned)frame_to_send->len, (unsigned)frame_to_send->buf[0]);
            _last_retry_attempt = 0;
            _last_send_time = now;
            if (noteFrameActivity(now, frame_to_send->len)) {
              requestSyncModeConnection();
            }
            popSendQueue();
          } else {
            if (!isConnected()) {
              BLE_DEBUG_PRINTLN("writeBytes failed: connection lost, dropping frame");
              _last_retry_attempt = 0;
              _last_send_time = 0;
              popSendQueue();
            } else {
              BLE_DEBUG_PRINTLN("writeBytes failed (buffer full), keeping frame for retry, queue=%u", (unsigned)send_queue.size());
              _last_retry_attempt = now;
            }
          }
        }
      }
    }
  }
  
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
  
  unsigned long now = millis();
  if (isConnected() && _sync_mode && _last_activity_time > 0 && 
      send_queue.isEmpty() && recv_queue.isEmpty()) {
    if (now - _last_activity_time >= BLE_SYNC_INACTIVITY_TIMEOUT_MS) {
      requestDefaultConnection();
    }
  }
  
  if (_isEnabled && !isConnected() && _conn_handle == BLE_CONN_HANDLE_INVALID) {
    if (now - _last_health_check >= BLE_HEALTH_CHECK_INTERVAL) {
      _last_health_check = now;
      
      if (!isAdvertising()) {
        BLE_DEBUG_PRINTLN("SerialBLEInterface: advertising watchdog - advertising stopped, restarting");
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        if (pAdvertising) {
          pAdvertising->start(0);
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
  
  pServer->updateConnParams(_conn_handle,
                            BLE_SYNC_MIN_CONN_INTERVAL,
                            BLE_SYNC_MAX_CONN_INTERVAL,
                            BLE_SYNC_SLAVE_LATENCY,
                            BLE_SYNC_CONN_SUP_TIMEOUT);
  BLE_DEBUG_PRINTLN("Sync mode connection parameter update requested successfully");
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
  
  pServer->updateConnParams(_conn_handle,
                            BLE_MIN_CONN_INTERVAL,
                            BLE_MAX_CONN_INTERVAL,
                            BLE_SLAVE_LATENCY,
                            BLE_CONN_SUP_TIMEOUT);
  BLE_DEBUG_PRINTLN("Default connection parameter update requested successfully");
}
