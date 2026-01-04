#include "SerialBLEInterface.h"
#include "../SerialBLECommon.h"
#include <stdio.h>
#include <string.h>
#include "ble_gap.h"
#include "ble_hci.h"

SerialBLEInterface* SerialBLEInterface::instance = nullptr;

void SerialBLEInterface::onConnect(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: connected handle=0x%04X", connection_handle);
  if (instance) {
    if (Bluefruit.connected() > 1) {
      uint32_t err_code = sd_ble_gap_disconnect(connection_handle, BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION);
      if (err_code != NRF_SUCCESS) {
        BLE_DEBUG_PRINTLN("SerialBLEInterface: failed to disconnect second connection: 0x%08lX", err_code);
      } else {
        BLE_DEBUG_PRINTLN("SerialBLEInterface: rejecting second connection, already have %d connection", Bluefruit.connected() - 1);
      }
      return;
    }
    
    instance->_conn_handle = connection_handle;
    instance->_isDeviceConnected = false;
    instance->clearBuffers(); // this seems redundant, but there were edge cases where stuff stuck in the buffers on rapid disconnect-connects
  }
}

void SerialBLEInterface::onDisconnect(uint16_t connection_handle, uint8_t reason) {
#if BLE_DEBUG_LOGGING
  const char* initiator;
  if (reason == BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION) {
    initiator = "local";
  } else if (reason == BLE_HCI_CONNECTION_TIMEOUT) {
    initiator = "timeout";
  } else {
    initiator = "remote";
  }
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disconnected handle=0x%04X reason=0x%02X (initiated by %s)", 
                    connection_handle, reason, initiator);
#endif
  if (instance) {
    if (instance->_conn_handle == connection_handle) {
      instance->_conn_handle = BLE_CONN_HANDLE_INVALID;
      instance->_isDeviceConnected = false;
      instance->clearBuffers();
      instance->_last_health_check = millis();
    }
  }
}

void SerialBLEInterface::onSecured(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: onSecured handle=0x%04X", connection_handle);
  if (instance) {
    if (instance->isValidConnection(connection_handle, true)) {
      instance->_isDeviceConnected = true;
      
      ble_gap_conn_params_t conn_params;
      conn_params.min_conn_interval = BLE_MIN_CONN_INTERVAL;
      conn_params.max_conn_interval = BLE_MAX_CONN_INTERVAL;
      conn_params.slave_latency = BLE_SLAVE_LATENCY;
      conn_params.conn_sup_timeout = BLE_CONN_SUP_TIMEOUT;
      
      uint32_t err_code = sd_ble_gap_conn_param_update(connection_handle, &conn_params);
      if (err_code == NRF_SUCCESS) {
        BLE_DEBUG_PRINTLN("Connection parameter update requested: %u-%ums interval, latency=%u, %ums timeout",
                         conn_params.min_conn_interval * 5 / 4,
                         conn_params.max_conn_interval * 5 / 4,
                         conn_params.slave_latency,
                         conn_params.conn_sup_timeout * 10);
      } else {
        BLE_DEBUG_PRINTLN("Failed to request connection parameter update: %lu", err_code);
      }
    } else {
      BLE_DEBUG_PRINTLN("onSecured: ignoring stale/duplicate callback");
    }
  }
}

bool SerialBLEInterface::onPairingPasskey(uint16_t connection_handle, uint8_t const passkey[6], bool match_request) {
  (void)connection_handle;
  (void)passkey;
  BLE_DEBUG_PRINTLN("SerialBLEInterface: pairing passkey request match=%d", match_request);
  return true;
}

void SerialBLEInterface::onPairingComplete(uint16_t connection_handle, uint8_t auth_status) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: pairing complete handle=0x%04X status=%u", connection_handle, auth_status);
  if (instance) {
    if (instance->isValidConnection(connection_handle)) {
      if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS) {
        BLE_DEBUG_PRINTLN("SerialBLEInterface: pairing successful");
      } else {
        BLE_DEBUG_PRINTLN("SerialBLEInterface: pairing failed, disconnecting");
        instance->disconnect();
      }
    } else {
      BLE_DEBUG_PRINTLN("onPairingComplete: ignoring stale callback");
    }
  }
}

void SerialBLEInterface::onBLEEvent(ble_evt_t* evt) {
  if (!instance) return;
  
  if (evt->header.evt_id == BLE_GAP_EVT_CONN_PARAM_UPDATE) {
    uint16_t conn_handle = evt->evt.gap_evt.conn_handle;
    if (instance->isValidConnection(conn_handle)) {
      ble_gap_conn_params_t* params = &evt->evt.gap_evt.params.conn_param_update.conn_params;
      uint16_t min_interval = params->min_conn_interval;
      uint16_t max_interval = params->max_conn_interval;
      uint16_t latency = params->slave_latency;
      uint16_t timeout = params->conn_sup_timeout;
      
      BLE_DEBUG_PRINTLN("CONN_PARAM_UPDATE: handle=0x%04X, min_interval=%u, max_interval=%u, latency=%u, timeout=%u",
                       conn_handle, min_interval, max_interval, latency, timeout);
      
      if (latency == BLE_SYNC_SLAVE_LATENCY &&
          timeout == BLE_SYNC_CONN_SUP_TIMEOUT &&
          min_interval >= BLE_SYNC_MIN_CONN_INTERVAL &&
          max_interval <= BLE_SYNC_MAX_CONN_INTERVAL) {
        if (!instance->_sync_mode) {
          BLE_DEBUG_PRINTLN("Sync mode confirmed by connection parameters");
          instance->_sync_mode = true;
          instance->_last_activity_time = millis();
        }
      } else if (latency == BLE_SLAVE_LATENCY &&
                 timeout == BLE_CONN_SUP_TIMEOUT &&
                 min_interval >= BLE_MIN_CONN_INTERVAL &&
                 max_interval <= BLE_MAX_CONN_INTERVAL) {
        if (instance->_sync_mode) {
          BLE_DEBUG_PRINTLN("Default mode confirmed by connection parameters");
          instance->_sync_mode = false;
        }
      }
      instance->_conn_param_update_pending = false;
    }
  } else if (evt->header.evt_id == BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST) {
    uint16_t conn_handle = evt->evt.gap_evt.conn_handle;
    if (instance->isValidConnection(conn_handle)) {
      BLE_DEBUG_PRINTLN("CONN_PARAM_UPDATE_REQUEST: handle=0x%04X, min_interval=%u, max_interval=%u, latency=%u, timeout=%u",
                       conn_handle,
                       evt->evt.gap_evt.params.conn_param_update_request.conn_params.min_conn_interval,
                       evt->evt.gap_evt.params.conn_param_update_request.conn_params.max_conn_interval,
                       evt->evt.gap_evt.params.conn_param_update_request.conn_params.slave_latency,
                       evt->evt.gap_evt.params.conn_param_update_request.conn_params.conn_sup_timeout);
      
      uint32_t err_code = sd_ble_gap_conn_param_update(conn_handle, NULL);
      if (err_code == NRF_SUCCESS) {
        BLE_DEBUG_PRINTLN("Accepted CONN_PARAM_UPDATE_REQUEST (using PPCP)");
      } else {
        BLE_DEBUG_PRINTLN("ERROR: Failed to accept CONN_PARAM_UPDATE_REQUEST: 0x%08X", err_code);
      }
    } else {
      BLE_DEBUG_PRINTLN("CONN_PARAM_UPDATE_REQUEST: ignoring stale callback for handle=0x%04X", conn_handle);
    }
  }
}

void SerialBLEInterface::begin(const char* device_name, uint32_t pin_code) {
  instance = this;

  char charpin[20];
  snprintf(charpin, sizeof(charpin), "%lu", (unsigned long)pin_code);
  
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  
  ble_gap_conn_params_t ppcp_params;
  ppcp_params.min_conn_interval = BLE_MIN_CONN_INTERVAL;
  ppcp_params.max_conn_interval = BLE_MAX_CONN_INTERVAL;
  ppcp_params.slave_latency = BLE_SLAVE_LATENCY;
  ppcp_params.conn_sup_timeout = BLE_CONN_SUP_TIMEOUT;
  
  uint32_t err_code = sd_ble_gap_ppcp_set(&ppcp_params);
  if (err_code == NRF_SUCCESS) {
    BLE_DEBUG_PRINTLN("PPCP set: %u-%ums interval, latency=%u, %ums timeout",
                     ppcp_params.min_conn_interval * 5 / 4,
                     ppcp_params.max_conn_interval * 5 / 4,
                     ppcp_params.slave_latency,
                     ppcp_params.conn_sup_timeout * 10);
  } else {
    BLE_DEBUG_PRINTLN("Failed to set PPCP: %lu", err_code);
  }
  
  Bluefruit.setTxPower(BLE_TX_POWER);
  Bluefruit.setName(device_name);

  Bluefruit.Security.setMITM(true);
  Bluefruit.Security.setPIN(charpin);
  Bluefruit.Security.setIOCaps(true, false, false);
  Bluefruit.Security.setPairPasskeyCallback(onPairingPasskey);
  Bluefruit.Security.setPairCompleteCallback(onPairingComplete);

  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);
  Bluefruit.Security.setSecuredCallback(onSecured);

  Bluefruit.setEventCallback(onBLEEvent);

  bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  bleuart.begin();
  bleuart.setRxCallback(onBleUartRX);

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);

  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.setInterval(BLE_ADV_INTERVAL_MIN, BLE_ADV_INTERVAL_MAX);
  Bluefruit.Advertising.setFastTimeout(BLE_ADV_FAST_TIMEOUT);

  Bluefruit.Advertising.restartOnDisconnect(true);

}

void SerialBLEInterface::clearBuffers() {
  clearTransferState();
  bleuart.flush();
}

bool SerialBLEInterface::isValidConnection(uint16_t handle, bool requireWaitingForSecurity) const {
  if (_conn_handle != handle) {
    return false;
  }
  BLEConnection* conn = Bluefruit.Connection(handle);
  if (conn == nullptr || !conn->connected()) {
    return false;
  }
  if (requireWaitingForSecurity && _isDeviceConnected) {
    return false;
  }
  return true;
}

bool SerialBLEInterface::isAdvertising() const {
  ble_gap_addr_t adv_addr;
  uint32_t err_code = sd_ble_gap_adv_addr_get(0, &adv_addr);
  (void)adv_addr;  // address not needed, only return code
  return (err_code == NRF_SUCCESS);
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();
  _last_health_check = millis();

  Bluefruit.Advertising.start(0);
}

void SerialBLEInterface::disconnect() {
  if (_conn_handle != BLE_CONN_HANDLE_INVALID) {
    sd_ble_gap_disconnect(_conn_handle, BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION);
  }
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disable");

  disconnect();
  Bluefruit.Advertising.stop();
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

      if (!throttle_active && send_interval_ok) {
        SerialBLEFrame* frame_to_send = send_queue.peekFront();
        if (frame_to_send) {
          size_t written = bleuart.write(frame_to_send->buf, frame_to_send->len);
          if (written == frame_to_send->len) {
            BLE_DEBUG_PRINTLN("writeBytes: sz=%u, hdr=%u", (unsigned)frame_to_send->len, (unsigned)frame_to_send->buf[0]);
            _last_retry_attempt = 0;
            _last_send_time = now;
            if (noteFrameActivity(now, frame_to_send->len)) {
              requestSyncModeConnection();
            }
            popSendQueue();
          } else if (written > 0) {
            BLE_DEBUG_PRINTLN("writeBytes: partial write, sent=%u of %u, dropping corrupted frame", (unsigned)written, (unsigned)frame_to_send->len);
            _last_retry_attempt = 0;
            _last_send_time = now;
            popSendQueue();
          } else {
            if (!isConnected()) {
              BLE_DEBUG_PRINTLN("writeBytes failed: connection lost, dropping frame");
              _last_retry_attempt = 0;
              popSendQueue();
            } else {
              BLE_DEBUG_PRINTLN("writeBytes failed (buffer full), keeping frame for retry");
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
        Bluefruit.Advertising.start(0);
      }
    }
  }
  
  return 0;
}

void SerialBLEInterface::onBleUartRX(uint16_t conn_handle) {
  if (!instance) {
    return;
  }
  
  if (instance->_conn_handle != conn_handle || !instance->isConnected()) {
    while (instance->bleuart.available() > 0) {
      instance->bleuart.read();
    }
    return;
  }
  
  while (instance->bleuart.available() > 0) {
    if (instance->recv_queue.isFull()) {
      while (instance->bleuart.available() > 0) {
        instance->bleuart.read();
      }
      BLE_DEBUG_PRINTLN("onBleUartRX: recv queue full, dropping data");
      break;
    }
    
    int avail = instance->bleuart.available();
    
    if (avail > MAX_FRAME_SIZE) {
      BLE_DEBUG_PRINTLN("onBleUartRX: WARN: BLE RX overflow, avail=%d, draining all", avail);
      uint8_t drain_buf[BLE_RX_DRAIN_BUF_SIZE];
      while (instance->bleuart.available() > 0) {
        int chunk = instance->bleuart.available() > BLE_RX_DRAIN_BUF_SIZE ? BLE_RX_DRAIN_BUF_SIZE : instance->bleuart.available();
        instance->bleuart.readBytes(drain_buf, chunk);
      }
      continue;
    }
    
    int read_len = avail;
    SerialBLEFrame* frame = instance->recv_queue.getWriteSlot();
    if (frame) {
      frame->len = read_len;
      instance->bleuart.readBytes(frame->buf, read_len);
      instance->recv_queue.push();

      unsigned long now = millis();
      if (instance->noteFrameActivity(now, read_len)) {
        instance->requestSyncModeConnection();
      }
    }
  }
}

bool SerialBLEInterface::isConnected() const {
  return _isDeviceConnected && _conn_handle != BLE_CONN_HANDLE_INVALID && Bluefruit.connected() > 0;
}

bool SerialBLEInterface::isWriteBusy() const {
  return isWriteBusyCommon();
}

void SerialBLEInterface::requestSyncModeConnection() {
  if (!isConnected()) {
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
  
  ble_gap_conn_params_t conn_params;
  conn_params.min_conn_interval = BLE_SYNC_MIN_CONN_INTERVAL;
  conn_params.max_conn_interval = BLE_SYNC_MAX_CONN_INTERVAL;
  conn_params.slave_latency = BLE_SYNC_SLAVE_LATENCY;
  conn_params.conn_sup_timeout = BLE_SYNC_CONN_SUP_TIMEOUT;
  
  uint32_t err_code = sd_ble_gap_conn_param_update(_conn_handle, &conn_params);
  if (err_code == NRF_SUCCESS) {
    BLE_DEBUG_PRINTLN("Sync mode connection parameter update requested successfully");
  } else {
    _conn_param_update_pending = false;
    if (err_code != NRF_ERROR_BUSY) {
      BLE_DEBUG_PRINTLN("Failed to request sync mode connection: %lu", err_code);
    }
  }
}

void SerialBLEInterface::requestDefaultConnection() {
  if (!isConnected()) {
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
  
  ble_gap_conn_params_t conn_params;
  conn_params.min_conn_interval = BLE_MIN_CONN_INTERVAL;
  conn_params.max_conn_interval = BLE_MAX_CONN_INTERVAL;
  conn_params.slave_latency = BLE_SLAVE_LATENCY;
  conn_params.conn_sup_timeout = BLE_CONN_SUP_TIMEOUT;
  
  uint32_t err_code = sd_ble_gap_conn_param_update(_conn_handle, &conn_params);
  if (err_code == NRF_SUCCESS) {
    BLE_DEBUG_PRINTLN("Default connection parameter update requested successfully");
  } else {
    _conn_param_update_pending = false;
    if (err_code != NRF_ERROR_BUSY) {
      BLE_DEBUG_PRINTLN("Failed to request default connection: %lu", err_code);
    }
  }
}
