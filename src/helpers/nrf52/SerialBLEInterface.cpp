#include "SerialBLEInterface.h"
#include <stdio.h>
#include <string.h>
#include "ble_gap.h"
#ifndef NRF54_PLATFORM
#include "ble_hci.h"
#else
#include <target.h>
#endif

// Magic numbers came from actual testing
#define BLE_HEALTH_CHECK_INTERVAL  10000  // Advertising watchdog check every 10 seconds
#define BLE_RETRY_THROTTLE_MS      250    // Throttle retries to 250ms when queue buildup detected

// Connection parameters (units: interval=1.25ms, timeout=10ms)
#define BLE_MIN_CONN_INTERVAL      12     // 15ms
#define BLE_MAX_CONN_INTERVAL      24     // 30ms
#define BLE_SLAVE_LATENCY          4
#define BLE_CONN_SUP_TIMEOUT       200    // 2000ms

// Advertising parameters
#define BLE_ADV_INTERVAL_MIN       32     // 20ms (units: 0.625ms)
#define BLE_ADV_INTERVAL_MAX       244    // 152.5ms (units: 0.625ms)
#define BLE_ADV_FAST_TIMEOUT       30     // seconds

// RX drain buffer size for overflow protection
#define BLE_RX_DRAIN_BUF_SIZE      32

static SerialBLEInterface* instance = nullptr;

#ifdef NRF54_PLATFORM
#define BLE_BATT_UPDATE_INTERVAL   60000  // Battery Service level update every 60 seconds

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

// the nRF54 core always has a Battery Service, which reads 100% unless it's kept up to date
static BLEBas blebas;
static unsigned long last_batt_update = 0;

static void updateBatteryLevel() {
  int mv = board.getBattMilliVolts();
  int pct = (mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  blebas.notify((uint8_t)pct);  // also sets the value when no one is subscribed
}
#endif

void SerialBLEInterface::onConnect(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: connected handle=0x%04X", connection_handle);
  if (instance) {
    instance->_conn_handle = connection_handle;
    instance->_isDeviceConnected = false;
    instance->clearBuffers();
#ifdef NRF54_PLATFORM
    // nRF54 rejects writes before pairing with "write not permitted" rather than
    // "insufficient encryption", so phones won't start pairing themselves. Request it here.
    BLEConnection* conn = Bluefruit.Connection(connection_handle);
    if (conn) conn->requestPairing();
#endif
  }
}

void SerialBLEInterface::onDisconnect(uint16_t connection_handle, uint8_t reason) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disconnected handle=0x%04X reason=%u", connection_handle, reason);
  if (instance) {
    if (instance->_conn_handle == connection_handle) {
      instance->_conn_handle = BLE_CONN_HANDLE_INVALID;
      instance->_isDeviceConnected = false;
      instance->clearBuffers();
    }
  }
}

void SerialBLEInterface::onSecured(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: onSecured handle=0x%04X", connection_handle);
  if (instance) {
    if (instance->isValidConnection(connection_handle, true)) {
      instance->_isDeviceConnected = true;
      
      // Connection interval units: 1.25ms, supervision timeout units: 10ms
      // Apple: "The product will not read or use the parameters in the Peripheral Preferred Connection Parameters characteristic."
      // So we explicitly set it here to make Android & Apple match
#ifdef NRF54_PLATFORM
      // on nRF54, setConnInterval() on an active connection sends a conn param update request
      Bluefruit.Periph.setConnInterval(BLE_MIN_CONN_INTERVAL, BLE_MAX_CONN_INTERVAL);
#else
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
#endif
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

// not needed on nRF54, its BLE stack answers conn param update requests itself
#ifndef NRF54_PLATFORM
void SerialBLEInterface::onBLEEvent(ble_evt_t* evt) {
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
  }
}
#endif

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
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
 
  char dev_name[32+16];
  if (strcmp(name, "@@MAC") == 0) {
#ifdef NRF54_PLATFORM
    ble_gap_addr_t addr = Bluefruit.getAddr();
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
        addr.addr[5], addr.addr[4], addr.addr[3], addr.addr[2], addr.addr[1], addr.addr[0]);
#else
    ble_gap_addr_t addr;
    if (sd_ble_gap_addr_get(&addr) == NRF_SUCCESS) {
      sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr.addr[5], addr.addr[4], addr.addr[3], addr.addr[2], addr.addr[1], addr.addr[0]);
    }
#endif
  }
  sprintf(dev_name, "%s%s", prefix, name);

  // Connection interval units: 1.25ms, supervision timeout units: 10ms
#ifdef NRF54_PLATFORM
  Bluefruit.Periph.setConnSlaveLatency(BLE_SLAVE_LATENCY);
  Bluefruit.Periph.setConnSupervisionTimeout(BLE_CONN_SUP_TIMEOUT);
  Bluefruit.Periph.setConnInterval(BLE_MIN_CONN_INTERVAL, BLE_MAX_CONN_INTERVAL);
#else
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
#endif
  
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

#ifndef NRF54_PLATFORM
  Bluefruit.setEventCallback(onBLEEvent);
#endif

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

void SerialBLEInterface::clearBuffers() {
  send_queue_len = 0;
  recv_queue_len = 0;
  _last_retry_attempt = 0;
  bleuart.flush();
}

void SerialBLEInterface::shiftSendQueueLeft() {
  if (send_queue_len > 0) {
    send_queue_len--;
    for (uint8_t i = 0; i < send_queue_len; i++) {
      send_queue[i] = send_queue[i + 1];
    }
  }
}

void SerialBLEInterface::shiftRecvQueueLeft() {
  if (recv_queue_len > 0) {
    recv_queue_len--;
    for (uint8_t i = 0; i < recv_queue_len; i++) {
      recv_queue[i] = recv_queue[i + 1];
    }
  }
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
#ifdef NRF54_PLATFORM
  return Bluefruit.Advertising.isRunning();
#else
  ble_gap_addr_t adv_addr;
  uint32_t err_code = sd_ble_gap_adv_addr_get(0, &adv_addr);
  return (err_code == NRF_SUCCESS);
#endif
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
#ifdef NRF54_PLATFORM
    Bluefruit.disconnect(_conn_handle);
#else
    sd_ble_gap_disconnect(_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
#endif
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

  bool connected = isConnected();
  if (connected && len > 0) {
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
      BLE_DEBUG_PRINTLN("writeBytes: connection invalid, clearing send queue");
      send_queue_len = 0;
    } else {
      unsigned long now = millis();
      bool throttle_active = (_last_retry_attempt > 0 && (now - _last_retry_attempt) < BLE_RETRY_THROTTLE_MS);

      if (!throttle_active) {
        Frame frame_to_send = send_queue[0];

        size_t written = bleuart.write(frame_to_send.buf, frame_to_send.len);
        if (written == frame_to_send.len) {
          BLE_DEBUG_PRINTLN("writeBytes: sz=%u, hdr=%u", (unsigned)frame_to_send.len, (unsigned)frame_to_send.buf[0]);
          _last_retry_attempt = 0;
          shiftSendQueueLeft();
        } else if (written > 0) {
          BLE_DEBUG_PRINTLN("writeBytes: partial write, sent=%u of %u, dropping corrupted frame", (unsigned)written, (unsigned)frame_to_send.len);
          _last_retry_attempt = 0;
          shiftSendQueueLeft();
        } else {
          if (!isConnected()) {
            BLE_DEBUG_PRINTLN("writeBytes failed: connection lost, dropping frame");
            _last_retry_attempt = 0;
            shiftSendQueueLeft();
          } else {
            BLE_DEBUG_PRINTLN("writeBytes failed (buffer full), keeping frame for retry");
            _last_retry_attempt = now;
          }
        }
      }
    }
  }
  
  if (recv_queue_len > 0) {
    size_t len = recv_queue[0].len;
    memcpy(dest, recv_queue[0].buf, len);
    
    BLE_DEBUG_PRINTLN("readBytes: sz=%u, hdr=%u", (unsigned)len, (unsigned)dest[0]);
    
    shiftRecvQueueLeft();
    return len;
  }
  
  // Advertising watchdog: periodically check if advertising is running, restart if not
  // Only run when truly disconnected (no connection handle), not during connection establishment
  unsigned long now = millis();
#ifdef NRF54_PLATFORM
  if (_isEnabled && (last_batt_update == 0 || now - last_batt_update >= BLE_BATT_UPDATE_INTERVAL)) {
    last_batt_update = now;
    updateBatteryLevel();
  }
#endif
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
    if (instance->recv_queue_len >= FRAME_QUEUE_SIZE) {
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
