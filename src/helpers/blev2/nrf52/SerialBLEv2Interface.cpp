#ifdef BLEV2
#include "SerialBLEv2Interface.h"
#include <stdio.h>
#include <string.h>
#include "ble_gap.h"
#include "ble_hci.h"

// Magic numbers came from actual testing
#define BLE_HEALTH_CHECK_INTERVAL  10000  // Advertising watchdog check every 10 seconds
#define BLE_RETRY_THROTTLE_MS      250    // Throttle retries to 250ms when queue buildup detected
#define BLE_HVN_TX_SLOTS           3      // notification queue size set by configPrphBandwidth(BANDWIDTH_MAX)

// RX drain buffer size for overflow protection
#define BLE_RX_DRAIN_BUF_SIZE      32

static SerialBLEv2Interface* instance = nullptr;

void SerialBLEv2Interface::onConnect(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEv2Interface: connected handle=0x%04X", connection_handle);
  if (instance) {
    if (Bluefruit.connected() > 1) {
      BLE_DEBUG_PRINTLN("SerialBLEv2Interface: rejecting second connection");
      sd_ble_gap_disconnect(connection_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      return;
    }
    instance->_conn_handle = connection_handle;
    instance->_isDeviceConnected = false;
    instance->_tx_in_flight = 0;
    instance->bleuart.flush();
  }
}

void SerialBLEv2Interface::onDisconnect(uint16_t connection_handle, uint8_t reason) {
  BLE_DEBUG_PRINTLN("SerialBLEv2Interface: disconnected handle=0x%04X reason=%u", connection_handle, reason);
  if (instance) {
    if (instance->_conn_handle == connection_handle) {
      instance->_conn_handle = BLE_CONN_HANDLE_INVALID;
      instance->_isDeviceConnected = false;
      instance->bleuart.flush();
      instance->_last_health_check = millis();
    }
  }
}

void SerialBLEv2Interface::onSecured(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEv2Interface: onSecured handle=0x%04X", connection_handle);
  if (instance) {
    if (instance->isValidConnection(connection_handle, true)) {
      instance->_isDeviceConnected = true;
      
      // Connection interval units: 1.25ms, supervision timeout units: 10ms
      // Apple: "The product will not read or use the parameters in the Peripheral Preferred Connection Parameters characteristic."
      // So we explicitly set it here to make Android & Apple match
      ble_gap_conn_params_t conn_params;
      conn_params.min_conn_interval = BLE_MIN_CONN_INTERVAL;
      conn_params.max_conn_interval = BLE_MAX_CONN_INTERVAL;
      conn_params.slave_latency = BLE_SLAVE_LATENCY;
      conn_params.conn_sup_timeout = BLE_CONN_SUP_TIMEOUT;
      
      uint32_t err_code = sd_ble_gap_conn_param_update(connection_handle, &conn_params);
      if (err_code == NRF_SUCCESS) {
        BLE_DEBUG_PRINTLN("Connection parameter update requested: %u-%ums interval, latency=%u, %ums timeout",
                         conn_params.min_conn_interval * 5 / 4,  // convert to ms (1.25ms units)
                         conn_params.max_conn_interval * 5 / 4,
                         conn_params.slave_latency,
                         conn_params.conn_sup_timeout * 10);  // convert to ms (10ms units)
      } else {
        BLE_DEBUG_PRINTLN("Failed to request connection parameter update: %lu", err_code);
      }
    } else {
      BLE_DEBUG_PRINTLN("onSecured: ignoring stale/duplicate callback");
    }
  }
}

bool SerialBLEv2Interface::onPairingPasskey(uint16_t connection_handle, uint8_t const passkey[6], bool match_request) {
  (void)connection_handle;
  (void)passkey;
  BLE_DEBUG_PRINTLN("SerialBLEv2Interface: pairing passkey request match=%d", match_request);
  return true;
}

void SerialBLEv2Interface::onPairingComplete(uint16_t connection_handle, uint8_t auth_status) {
  BLE_DEBUG_PRINTLN("SerialBLEv2Interface: pairing complete handle=0x%04X status=%u", connection_handle, auth_status);
  if (instance) {
    if (instance->isValidConnection(connection_handle)) {
      if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS) {
        BLE_DEBUG_PRINTLN("SerialBLEv2Interface: pairing successful");
      } else {
        BLE_DEBUG_PRINTLN("SerialBLEv2Interface: pairing failed, disconnecting");
        instance->disconnect();
      }
    } else {
      BLE_DEBUG_PRINTLN("onPairingComplete: ignoring stale callback");
    }
  }
}

void SerialBLEv2Interface::onBLEEvent(ble_evt_t* evt) {
  if (!instance) return;
  
  if (evt->header.evt_id == BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST) {
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
  } else if (evt->header.evt_id == BLE_GATTS_EVT_HVN_TX_COMPLETE && evt->evt.gatts_evt.conn_handle == instance->_conn_handle) {
    // plain read-modify-write is safe: the loop task can't preempt the BLE task
    uint8_t done = evt->evt.gatts_evt.params.hvn_tx_complete.count, n = instance->_tx_in_flight;
    instance->_tx_in_flight = done < n ? n - done : 0;
  }
}

// the TX_COMPLETE handler may lower the count meanwhile, so clamp at 0 like it does
static void releaseTxSlots(volatile uint8_t* count, uint8_t n) {
  uint8_t v = *count;
  while (!__atomic_compare_exchange_n(count, &v, (uint8_t)(v > n ? v - n : 0), true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

void SerialBLEv2Interface::trySendNextFrame() {
  if (_last_retry_attempt > 0 && millis() - _last_retry_attempt < BLE_RETRY_THROTTLE_MS) return;

  // only write into free TX slots: otherwise bleuart.write() blocks the main loop for up to 100 ms
  while (isConnected() && !send_queue.isEmpty()) {
    BLEConnection* conn = Bluefruit.Connection(_conn_handle);
    if (conn == nullptr || !bleuart.notifyEnabled(_conn_handle)) {
      _last_retry_attempt = millis();   // back off, eg. notifications not enabled yet
      break;
    }
    const SerialBLEFrame& frame = send_queue.peek();
    uint16_t payload = conn->getMtu() - 3;
    uint8_t n = (frame.len + payload - 1) / payload;   // write() sends one notification per payload bytes
    if (_tx_in_flight > 0 && _tx_in_flight + n > BLE_HVN_TX_SLOTS) break;   // more than all slots: wait for none in flight
    __atomic_add_fetch(&_tx_in_flight, n, __ATOMIC_RELAXED);   // before write(): its TX_COMPLETE can arrive first
    if (bleuart.write(frame.buf, frame.len) == frame.len) {
      BLE_DEBUG_PRINTLN("writeBytes: sz=%u, hdr=%u", (unsigned)frame.len, (unsigned)frame.buf[0]);
      send_queue.pop();
    } else if (n > 1) {
      releaseTxSlots(&_tx_in_flight, n);
      BLE_DEBUG_PRINTLN("writeBytes failed part way, dropping frame");   // a retry would repeat what went out
      send_queue.pop();
    } else {
      __atomic_sub_fetch(&_tx_in_flight, 1, __ATOMIC_RELAXED);
      BLE_DEBUG_PRINTLN("writeBytes failed (buffer full), keeping frame for retry");
      _last_retry_attempt = millis();
      break;
    }
  }
}

void SerialBLEv2Interface::begin(const char* prefix, char* name, uint32_t pin_code) {
  instance = this;

  char charpin[20];
  snprintf(charpin, sizeof(charpin), "%lu", (unsigned long)pin_code);
  
  #ifdef DISABLE_BLE_LED  // use variant platformio.ini build_flags macro: -D DISABLE_BLE_LED
    Bluefruit.autoConnLed(false);  // false removes the automatic control of the LED by the Bluefruit library
  #else
    Bluefruit.autoConnLed(true);  // true leaves the default automatic control of the LED by the Bluefruit library
  #endif
  
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
 
  ble_gap_addr_t addr = {};
  sd_ble_gap_addr_get(&addr);
  char dev_name[32+16];
  makeBLEName(dev_name, sizeof(dev_name), prefix, name, addr.addr, true);

  // Connection interval units: 1.25ms, supervision timeout units: 10ms
  ble_gap_conn_params_t ppcp_params;
  ppcp_params.min_conn_interval = BLE_MIN_CONN_INTERVAL;
  ppcp_params.max_conn_interval = BLE_MAX_CONN_INTERVAL;
  ppcp_params.slave_latency = BLE_SLAVE_LATENCY;
  ppcp_params.conn_sup_timeout = BLE_CONN_SUP_TIMEOUT;
  
  uint32_t err_code = sd_ble_gap_ppcp_set(&ppcp_params);
  if (err_code == NRF_SUCCESS) {
    BLE_DEBUG_PRINTLN("PPCP set: %u-%ums interval, latency=%u, %ums timeout",
                     ppcp_params.min_conn_interval * 5 / 4,  // convert to ms (1.25ms units)
                     ppcp_params.max_conn_interval * 5 / 4,
                     ppcp_params.slave_latency,
                     ppcp_params.conn_sup_timeout * 10);  // convert to ms (10ms units)
  } else {
    BLE_DEBUG_PRINTLN("Failed to set PPCP: %lu", err_code);
  }
  
  Bluefruit.setTxPower(BLE_TX_POWER);
  Bluefruit.setName(dev_name);

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



  // Register DFU on the main BLE stack so paired clients can discover it
  // without switching the device into a separate OTA-only BLE mode first.
  bledfu.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  bledfu.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);

  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.setInterval(BLE_ADV_INTERVAL_MIN, BLE_ADV_INTERVAL_MAX);
  Bluefruit.Advertising.setFastTimeout(BLE_ADV_FAST_TIMEOUT);

  Bluefruit.Advertising.restartOnDisconnect(true);

}

void SerialBLEv2Interface::clearBuffers() {
  send_queue.clear();
  recv_queue.clear();
  _last_retry_attempt = 0;
  bleuart.flush();
}

bool SerialBLEv2Interface::isValidConnection(uint16_t handle, bool requireWaitingForSecurity) const {
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

bool SerialBLEv2Interface::isAdvertising() const {
  ble_gap_addr_t adv_addr;
  uint32_t err_code = sd_ble_gap_adv_addr_get(0, &adv_addr);
  return (err_code == NRF_SUCCESS);
}

void SerialBLEv2Interface::enable() {
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();
  _last_health_check = millis();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);
}

void SerialBLEv2Interface::disconnect() {
  if (_conn_handle != BLE_CONN_HANDLE_INVALID) {
    sd_ble_gap_disconnect(_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  }
}

void SerialBLEv2Interface::disable() {
  _isEnabled = false;
  BLE_DEBUG_PRINTLN("SerialBLEv2Interface: disable");

  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
  disconnect();
  _last_health_check = 0;
}

size_t SerialBLEv2Interface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%u", (unsigned)len);
    return 0;
  }

  bool connected = isConnected();
  if (connected && len > 0) {
    if (!send_queue.push(src, len)) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }
    trySendNextFrame();
    return len;
  }
  return 0;
}

size_t SerialBLEv2Interface::checkRecvFrame(uint8_t dest[]) {
  if (!isConnected()) {
    if (_was_connected) {   // once per link: clearing while idle could wipe the next link's first frame
      send_queue.clear();   // here rather than in the BLE callbacks, as only the consumer may clear
      recv_queue.clear();
      _was_connected = false;
    }
  } else {
    _was_connected = true;
    trySendNextFrame();   // frames that waited for a free TX slot
  }
  
  if (!recv_queue.isEmpty()) {
    size_t len = recv_queue.peek().len;
    memcpy(dest, recv_queue.peek().buf, len);
    
    BLE_DEBUG_PRINTLN("readBytes: sz=%u, hdr=%u", (unsigned)len, (unsigned)dest[0]);
    
    recv_queue.pop();
    return len;
  }
  
  unsigned long now = millis();
  // Advertising watchdog: periodically check if advertising is running, restart if not
  // Only run when truly disconnected (no connection handle), not during connection establishment
  if (_isEnabled && !isConnected() && _conn_handle == BLE_CONN_HANDLE_INVALID) {
    if (now - _last_health_check >= BLE_HEALTH_CHECK_INTERVAL) {
      _last_health_check = now;
      
      if (!isAdvertising()) {
        BLE_DEBUG_PRINTLN("SerialBLEv2Interface: advertising watchdog - advertising stopped, restarting");
        Bluefruit.Advertising.start(0);
      }
    }
  }
  
  return 0;
}

void SerialBLEv2Interface::onBleUartRX(uint16_t conn_handle) {
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
    
    uint8_t buf[MAX_FRAME_SIZE];
    int read_len = instance->bleuart.readBytes(buf, avail);
    if (!instance->recv_queue.push(buf, read_len)) {
      BLE_DEBUG_PRINTLN("onBleUartRX: recv queue full, dropping frame");
    }
  }
}

bool SerialBLEv2Interface::isConnected() const {
  return _isDeviceConnected && Bluefruit.connected() > 0;
}

bool SerialBLEv2Interface::isWriteBusy() const {
  return send_queue.used() >= (BLE_FRAME_QUEUE_SIZE * 2 / 3);
}

#endif
