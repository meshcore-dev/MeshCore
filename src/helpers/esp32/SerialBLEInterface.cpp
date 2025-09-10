#include "SerialBLEInterface.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  2000   // millis - increased for stability

void SerialBLEInterface::resetConnectionStats() {
  conn_stats.total_connections = 0;
  conn_stats.failed_connections = 0;
  conn_stats.disconnections = 0;
  conn_stats.timeouts = 0;
  conn_stats.last_disconnect_time = 0;
  conn_stats.consecutive_failures = 0;
}

void SerialBLEInterface::updateConnectionParameters() {
  if (!deviceConnected || connection_params_updated) return;
  
  // Update connection parameters for iOS optimization
  esp_ble_conn_update_params_t conn_params;
  
  // Use power-saving parameters if we detect instability, otherwise use optimal iOS parameters
  if (conn_stats.consecutive_failures >= 3) {
    conn_params.min_int = IOS_POWER_MIN_CONN_INTERVAL;
    conn_params.max_int = IOS_POWER_MAX_CONN_INTERVAL;
    conn_params.latency = IOS_POWER_SLAVE_LATENCY;
    conn_params.timeout = IOS_POWER_CONN_SUP_TIMEOUT;
    BLE_DEBUG_PRINTLN("Applied iOS power-saving connection parameters due to instability");
  } else {
    conn_params.min_int = IOS_MIN_CONN_INTERVAL;
    conn_params.max_int = IOS_MAX_CONN_INTERVAL;
    conn_params.latency = IOS_SLAVE_LATENCY;
    conn_params.timeout = IOS_CONN_SUP_TIMEOUT;
    BLE_DEBUG_PRINTLN("Applied iOS optimized connection parameters");
  }
  
  // Copy the device address for the connection parameter update
  memcpy(conn_params.bda, pServer->getPeerAddress(last_conn_id).getNative(), ESP_BD_ADDR_LEN);
  
  // Request connection parameter update
  esp_err_t ret = esp_ble_gap_update_conn_params(&conn_params);
  if (ret == ESP_OK) {
    connection_params_updated = true;
    BLE_DEBUG_PRINTLN("iOS connection parameter update requested successfully");
  } else {
    BLE_DEBUG_PRINTLN("Failed to update iOS connection parameters, error: %d", ret);
  }
}

bool SerialBLEInterface::shouldRetryConnection() {
  if (connection_retry_count >= MAX_CONNECTION_RETRIES) {
    return false;
  }
  
  unsigned long time_since_disconnect = millis() - conn_stats.last_disconnect_time;
  return time_since_disconnect >= CONNECTION_RETRY_DELAY;
}

void SerialBLEInterface::handleConnectionFailure() {
  conn_stats.failed_connections++;
  conn_stats.consecutive_failures++;
  connection_retry_count++;
  
  BLE_DEBUG_PRINTLN("Connection failure #%d (consecutive: %d)", 
                    conn_stats.failed_connections, conn_stats.consecutive_failures);
  
  // Exponential backoff for repeated failures
  unsigned long delay = ADVERT_RESTART_DELAY * (1 << min(conn_stats.consecutive_failures, 4));
  adv_restart_time = millis() + delay;
  
  BLE_DEBUG_PRINTLN("Will retry advertising in %lu ms", delay);
}

void SerialBLEInterface::begin(const char* device_name, uint32_t pin_code) {
  _pin_code = pin_code;

  // Create the BLE Device with iOS-optimized settings
  BLEDevice::init(device_name);
  BLEDevice::setSecurityCallbacks(this);
  
  // Set MTU to maximum supported by iOS (247 bytes for data)
  BLEDevice::setMTU(247);

  // Configure security with iOS-compatible settings
  BLESecurity sec;
  sec.setStaticPIN(pin_code);
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec.setCapability(ESP_IO_CAP_NONE); // No display, no keyboard
  sec.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  
  // Set TX power to maximum for better range and stability
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(this);

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create TX characteristic with iOS-optimized properties
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX, 
    BLECharacteristic::PROPERTY_READ | 
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE  // Add indication for reliability
  );
  pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  
  // Add descriptor for notifications
  BLE2902* p2902Descriptor = new BLE2902();
  p2902Descriptor->setNotifications(true);
  p2902Descriptor->setIndications(true);
  pTxCharacteristic->addDescriptor(p2902Descriptor);

  // Create RX characteristic
  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX, 
    BLECharacteristic::PROPERTY_WRITE | 
    BLECharacteristic::PROPERTY_WRITE_NR  // Add write without response
  );
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  pRxCharacteristic->setCallbacks(this);

  // Configure advertising with iOS-optimized parameters
  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Functions that help with iPhone connections
  pAdvertising->setMaxPreferred(0x12);
  
  // Set connectable and scannable for iOS compatibility
  pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  
  // Set device appearance as generic computer
  BLEDevice::setCustomGapHandler([](esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
      BLE_DEBUG_PRINTLN("Advertising data set complete");
    }
  });
}

// -------- BLESecurityCallbacks methods

uint32_t SerialBLEInterface::onPassKeyRequest() {
  BLE_DEBUG_PRINTLN("onPassKeyRequest()");
  return _pin_code;
}

void SerialBLEInterface::onPassKeyNotify(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onPassKeyNotify(%u)", pass_key);
}

bool SerialBLEInterface::onConfirmPIN(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onConfirmPIN(%u)", pass_key);
  return true;
}

bool SerialBLEInterface::onSecurityRequest() {
  BLE_DEBUG_PRINTLN("onSecurityRequest()");
  return true;  // allow
}

void SerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
  if (cmpl.success) {
    BLE_DEBUG_PRINTLN("Authentication Success");
    conn_stats.consecutive_failures = 0; // Reset failure count on success
    connection_retry_count = 0;
  } else {
    BLE_DEBUG_PRINTLN("Authentication Failure, reason: %d", cmpl.fail_reason);
    handleConnectionFailure();
    
    // Disconnect and restart advertising with delay
    pServer->disconnect(pServer->getConnId());
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- BLEServerCallbacks methods

void SerialBLEInterface::onConnect(BLEServer* pServer) {
  // Basic connection setup
}

void SerialBLEInterface::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", 
                    param->connect.conn_id, pServer->getPeerMTU(param->connect.conn_id));
  
  last_conn_id = param->connect.conn_id;
  last_connection_time = millis();
  conn_stats.total_connections++;
  connection_params_updated = false;
  
  // Detect iOS device based on connection parameters or other characteristics
  // This is a heuristic - iOS devices often have specific patterns
  uint16_t mtu = pServer->getPeerMTU(param->connect.conn_id);
  if (mtu >= 185) { // iOS typically negotiates higher MTU
    ios_device_detected = true;
    BLE_DEBUG_PRINTLN("iOS device detected, optimizing parameters");
  }
}

void SerialBLEInterface::onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", pServer->getPeerMTU(param->mtu.conn_id));
  
  // Connection is now fully established
  deviceConnected = true;
  
  // Update connection parameters for iOS optimization after a short delay
  if (ios_device_detected) {
    // Schedule parameter update
    connection_supervision_timeout = millis() + 1000; // 1 second delay
  }
}

void SerialBLEInterface::onDisconnect(BLEServer* pServer) {
  BLE_DEBUG_PRINTLN("onDisconnect()");
  
  deviceConnected = false;
  connection_params_updated = false;
  ios_device_detected = false;
  
  conn_stats.disconnections++;
  conn_stats.last_disconnect_time = millis();
  
  // Check if this was a stable connection
  unsigned long connection_duration = millis() - last_connection_time;
  if (connection_duration >= CONNECTION_STABILITY_TIME) {
    BLE_DEBUG_PRINTLN("Stable connection lasted %lu ms", connection_duration);
    conn_stats.consecutive_failures = 0; // Reset failure count for stable connection
    connection_retry_count = 0;
  } else {
    BLE_DEBUG_PRINTLN("Short connection lasted only %lu ms", connection_duration);
    conn_stats.consecutive_failures++;
  }
  
  if (_isEnabled) {
    // Determine restart delay based on connection stability
    unsigned long delay = shouldRetryConnection() ? ADVERT_RESTART_DELAY : 
                         ADVERT_RESTART_DELAY * (1 << min(conn_stats.consecutive_failures, 3));
    adv_restart_time = millis() + delay;
    
    BLE_DEBUG_PRINTLN("Will restart advertising in %lu ms", delay);
  }
}

// -------- BLECharacteristicCallbacks methods

void SerialBLEInterface::onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
  uint8_t* rxValue = pCharacteristic->getData();
  int len = pCharacteristic->getLength();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
  } else if (recv_queue_len >= FRAME_QUEUE_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
  } else {
    recv_queue[recv_queue_len].len = len;
    memcpy(recv_queue[recv_queue_len].buf, rxValue, len);
    recv_queue_len++;
  }
}

// ---------- public methods

void SerialBLEInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();
  resetConnectionStats();

  // Start the service
  pService->start();

  // Start advertising with iOS-optimized parameters (Apple's exact recommended intervals)
  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  pAdvertising->setMinInterval(IOS_ADV_FAST_INTERVAL);
  pAdvertising->setMaxInterval(IOS_ADV_FAST_INTERVAL);
  pAdvertising->start();
  
  adv_restart_time = 0;
  BLE_DEBUG_PRINTLN("BLE enabled and advertising started");
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  pServer->getAdvertising()->stop();
  if (deviceConnected) {
    pServer->disconnect(last_conn_id);
  }
  pService->stop();
  
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = 0;
  printConnectionStats();
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

// Reduced write interval for better responsiveness
#define BLE_WRITE_MIN_INTERVAL   50  // Reduced from 60ms to 50ms

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  // Handle scheduled connection parameter updates
  if (connection_supervision_timeout && millis() >= connection_supervision_timeout) {
    updateConnectionParameters();
    connection_supervision_timeout = 0;
  }
  
  // Send queued data
  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    
    // Use notifications for better iOS compatibility
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }

  // Check for received data
  if (recv_queue_len > 0) {   // check recv queue
    size_t len = recv_queue[0].len;   // take from top of queue
    memcpy(dest, recv_queue[0].buf, len);

    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", len, (uint32_t) dest[0]);

    recv_queue_len--;
    for (int i = 0; i < recv_queue_len; i++) {   // delete top item from queue
      recv_queue[i] = recv_queue[i + 1];
    }
    return len;
  }

  // Update connection status
  if (pServer->getConnectedCount() == 0) {
    deviceConnected = false;
  }

  // Handle connection state changes
  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");
      
      // Set advertising parameters based on failure history (Apple's exact intervals)
      BLEAdvertising* pAdvertising = pServer->getAdvertising();
      if (conn_stats.consecutive_failures > 4) {
        // Use Apple's 4th slow interval for severe failures
        pAdvertising->setMinInterval(IOS_ADV_SLOW_INTERVAL_4);
        pAdvertising->setMaxInterval(IOS_ADV_SLOW_INTERVAL_4);
      } else if (conn_stats.consecutive_failures > 3) {
        // Use Apple's 3rd slow interval for repeated failures
        pAdvertising->setMinInterval(IOS_ADV_SLOW_INTERVAL_3);
        pAdvertising->setMaxInterval(IOS_ADV_SLOW_INTERVAL_3);
      } else if (conn_stats.consecutive_failures > 2) {
        // Use Apple's 2nd slow interval for some failures
        pAdvertising->setMinInterval(IOS_ADV_SLOW_INTERVAL_2);
        pAdvertising->setMaxInterval(IOS_ADV_SLOW_INTERVAL_2);
      } else if (conn_stats.consecutive_failures > 1) {
        // Use Apple's 1st slow interval for minor failures
        pAdvertising->setMinInterval(IOS_ADV_SLOW_INTERVAL_1);
        pAdvertising->setMaxInterval(IOS_ADV_SLOW_INTERVAL_1);
      } else {
        // Use fast advertising for normal disconnects
        pAdvertising->setMinInterval(IOS_ADV_FAST_INTERVAL);
        pAdvertising->setMaxInterval(IOS_ADV_FAST_INTERVAL);
      }
      
      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      // Stop advertising when connected
      pServer->getAdvertising()->stop();
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  // Restart advertising if needed
  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      pServer->getAdvertising()->start();
    }
    adv_restart_time = 0;
  }
  
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;
}

void SerialBLEInterface::printConnectionStats() {
  BLE_DEBUG_PRINTLN("=== BLE Connection Statistics ===");
  BLE_DEBUG_PRINTLN("Total connections: %u", conn_stats.total_connections);
  BLE_DEBUG_PRINTLN("Failed connections: %u", conn_stats.failed_connections);
  BLE_DEBUG_PRINTLN("Disconnections: %u", conn_stats.disconnections);
  BLE_DEBUG_PRINTLN("Consecutive failures: %u", conn_stats.consecutive_failures);
  if (conn_stats.total_connections > 0) {
    float success_rate = 100.0f * (conn_stats.total_connections - conn_stats.failed_connections) / conn_stats.total_connections;
    BLE_DEBUG_PRINTLN("Success rate: %.1f%%", success_rate);
  }
  BLE_DEBUG_PRINTLN("================================");
}

bool SerialBLEInterface::isConnectionStable() const {
  if (!deviceConnected) return false;
  
  unsigned long connection_duration = millis() - last_connection_time;
  return connection_duration >= CONNECTION_STABILITY_TIME && conn_stats.consecutive_failures < 3;
}
