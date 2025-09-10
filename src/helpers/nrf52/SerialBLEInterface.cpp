#include "SerialBLEInterface.h"

static SerialBLEInterface* instance;

void SerialBLEInterface::resetConnectionStats() {
  conn_stats.total_connections = 0;
  conn_stats.failed_connections = 0;
  conn_stats.disconnections = 0;
  conn_stats.timeouts = 0;
  conn_stats.last_disconnect_time = 0;
  conn_stats.consecutive_failures = 0;
}

void SerialBLEInterface::updateConnectionParameters() {
  if (!_isDeviceConnected || connection_params_updated) return;
  
  // Get current connection handle
  uint16_t conn_handle = Bluefruit.connHandle();
  if (conn_handle == BLE_CONN_HANDLE_INVALID) return;
  
  // Update connection parameters for iOS optimization
  ble_gap_conn_params_t conn_params;
  
  // Use power-saving parameters if we detect instability, otherwise use optimal iOS parameters
  if (conn_stats.consecutive_failures >= 3) {
    conn_params.min_conn_interval = NRF_IOS_POWER_MIN_CONN_INTERVAL;
    conn_params.max_conn_interval = NRF_IOS_POWER_MAX_CONN_INTERVAL;
    conn_params.slave_latency = NRF_IOS_POWER_SLAVE_LATENCY;
    conn_params.conn_sup_timeout = NRF_IOS_POWER_CONN_SUP_TIMEOUT;
    BLE_DEBUG_PRINTLN("Applied iOS power-saving connection parameters due to instability");
  } else {
    conn_params.min_conn_interval = NRF_IOS_MIN_CONN_INTERVAL;
    conn_params.max_conn_interval = NRF_IOS_MAX_CONN_INTERVAL;
    conn_params.slave_latency = NRF_IOS_SLAVE_LATENCY;
    conn_params.conn_sup_timeout = NRF_IOS_CONN_SUP_TIMEOUT;
    BLE_DEBUG_PRINTLN("Applied iOS optimized connection parameters");
  }
  
  if (sd_ble_gap_conn_param_update(conn_handle, &conn_params) == NRF_SUCCESS) {
    connection_params_updated = true;
    BLE_DEBUG_PRINTLN("iOS connection parameter update requested successfully");
  } else {
    BLE_DEBUG_PRINTLN("Failed to update iOS connection parameters");
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
}

void SerialBLEInterface::onConnect(uint16_t connection_handle) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: connected, handle=%d", connection_handle);
  if(instance){
    instance->_isDeviceConnected = true;
    instance->last_connection_time = millis();
    instance->conn_stats.total_connections++;
    instance->connection_params_updated = false;
    
    // Detect iOS device based on connection characteristics
    // We'll use a simple heuristic for now - assume iOS for better compatibility
    instance->ios_device_detected = true;
    BLE_DEBUG_PRINTLN("Device connected, applying iOS optimizations");
    // Schedule parameter update after connection is fully established
    instance->connection_supervision_timeout = millis() + 1000; // 1 second delay
    
    // Reset failure counters on successful connection
    instance->conn_stats.consecutive_failures = 0;
    instance->connection_retry_count = 0;
    
    // Stop advertising automatically handled by Bluefruit stack
  }
}

void SerialBLEInterface::onDisconnect(uint16_t connection_handle, uint8_t reason) {
  BLE_DEBUG_PRINTLN("SerialBLEInterface: disconnected handle=%d, reason=%d", connection_handle, reason);
  if(instance){
    instance->_isDeviceConnected = false;
    instance->connection_params_updated = false;
    instance->ios_device_detected = false;
    
    instance->conn_stats.disconnections++;
    instance->conn_stats.last_disconnect_time = millis();
    
    // Check if this was a stable connection
    unsigned long connection_duration = millis() - instance->last_connection_time;
    if (connection_duration >= CONNECTION_STABILITY_TIME) {
      BLE_DEBUG_PRINTLN("Stable connection lasted %lu ms", connection_duration);
      instance->conn_stats.consecutive_failures = 0; // Reset failure count for stable connection
      instance->connection_retry_count = 0;
    } else {
      BLE_DEBUG_PRINTLN("Short connection lasted only %lu ms", connection_duration);
      instance->conn_stats.consecutive_failures++;
    }
    
    // Handle different disconnect reasons
    switch(reason) {
      case BLE_HCI_CONNECTION_TIMEOUT:
        instance->conn_stats.timeouts++;
        BLE_DEBUG_PRINTLN("Connection timeout detected");
        break;
      case BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION:
        BLE_DEBUG_PRINTLN("Remote user terminated connection");
        break;
      case BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION:
        BLE_DEBUG_PRINTLN("Local host terminated connection");
        break;
      default:
        BLE_DEBUG_PRINTLN("Other disconnect reason: %d", reason);
        break;
    }
    
    // Restart advertising with adaptive parameters
    if (instance->_isEnabled) {
      instance->startAdv();
    }
  }
}

void SerialBLEInterface::begin(const char* device_name, uint32_t pin_code) {

  instance = this;

  char charpin[20];
  sprintf(charpin, "%d", pin_code);

  // Configure Bluefruit with iOS-optimized settings
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  
  // Optimize connection parameters for iOS
  Bluefruit.configPrphConn(
    247,                           // MTU max
    BLE_GAP_EVENT_LENGTH_MIN,      // event length
    16,                            // hvn queue size
    16                             // write cmd queue size
  );
  
  Bluefruit.begin();
  
  // Set maximum TX power for better range and stability
  Bluefruit.setTxPower(4);
  Bluefruit.setName(device_name);

  // Configure security for iOS compatibility
  Bluefruit.Security.setMITM(true);
  Bluefruit.Security.setPIN(charpin);
  Bluefruit.Security.setIOCaps(false, false, false); // No display, no yes/no, no keyboard

  // Set connection and disconnect callbacks
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  // Configure and start the BLE Uart service with iOS-optimized settings
  bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  bleuart.begin();
  
  BLE_DEBUG_PRINTLN("BLE UART service initialized with iOS optimizations");
}

void SerialBLEInterface::startAdv() {

  BLE_DEBUG_PRINTLN("SerialBLEInterface: starting advertising");
  
  // Clean restart if already advertising
  if(Bluefruit.Advertising.isRunning()){
    BLE_DEBUG_PRINTLN("SerialBLEInterface: already advertising, stopping to allow clean restart");
    Bluefruit.Advertising.stop();
    delay(100); // Small delay to ensure clean stop
  }

  Bluefruit.Advertising.clearData(); // clear advertising data
  Bluefruit.ScanResponse.clearData(); // clear scan response data
  
  // Configure advertising packet for iOS optimization
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  
  // Include the BLE UART (AKA 'NUS') 128-bit UUID
  Bluefruit.Advertising.addService(bleuart);
  
  // Add appearance as generic computer for better iOS recognition
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_GENERIC_COMPUTER);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();
  
  // Add some additional service data for iOS compatibility
  Bluefruit.ScanResponse.addService(bleuart);

  // Configure advertising intervals based on failure history (Apple's exact intervals)
  uint16_t fast_interval, slow_interval;
  if (conn_stats.consecutive_failures > 4) {
    // Use Apple's 4th slow interval for severe failures
    fast_interval = slow_interval = NRF_IOS_ADV_SLOW_INTERVAL_4;
    BLE_DEBUG_PRINTLN("Using Apple's 4th slow advertising interval due to severe failures");
  } else if (conn_stats.consecutive_failures > 3) {
    // Use Apple's 3rd slow interval for repeated failures
    fast_interval = slow_interval = NRF_IOS_ADV_SLOW_INTERVAL_3;
    BLE_DEBUG_PRINTLN("Using Apple's 3rd slow advertising interval due to repeated failures");
  } else if (conn_stats.consecutive_failures > 2) {
    // Use Apple's 2nd slow interval for some failures
    fast_interval = slow_interval = NRF_IOS_ADV_SLOW_INTERVAL_2;
    BLE_DEBUG_PRINTLN("Using Apple's 2nd slow advertising interval due to some failures");
  } else if (conn_stats.consecutive_failures > 1) {
    // Use Apple's 1st slow interval for minor failures
    fast_interval = slow_interval = NRF_IOS_ADV_SLOW_INTERVAL_1;
    BLE_DEBUG_PRINTLN("Using Apple's 1st slow advertising interval due to minor failures");
  } else {
    // Use fast advertising for normal operation
    fast_interval = NRF_IOS_ADV_FAST_INTERVAL;
    slow_interval = NRF_IOS_ADV_SLOW_INTERVAL_1;
  }

  /* Start Advertising with iOS-optimized parameters
   * - Disable auto advertising restart (we handle it manually)
   * - Use Apple recommended intervals
   * - Timeout for fast mode
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(false); // we handle restart in onDisconnect
  Bluefruit.Advertising.setInterval(fast_interval, slow_interval);
  Bluefruit.Advertising.setFastTimeout(NRF_IOS_ADV_FAST_TIMEOUT);
  Bluefruit.Advertising.start(0); // 0 = Don't stop advertising after n seconds

}

void SerialBLEInterface::stopAdv() {

  BLE_DEBUG_PRINTLN("SerialBLEInterface: stopping advertising");
  
  // We only want to stop advertising if it's running, otherwise an error is logged
  if(!Bluefruit.Advertising.isRunning()){
    return;
  }

  // Stop advertising
  Bluefruit.Advertising.stop();
}

// ---------- public methods

void SerialBLEInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();
  resetConnectionStats();

  // Start advertising
  startAdv();
  BLE_DEBUG_PRINTLN("BLE enabled and advertising started");
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  // Disconnect any active connections
  uint16_t conn_handle = Bluefruit.connHandle();
  if (conn_handle != BLE_CONN_HANDLE_INVALID) {
    Bluefruit.disconnect(conn_handle);
  }

  // Stop advertising
  Bluefruit.Advertising.restartOnDisconnect(false);
  stopAdv();
  Bluefruit.Advertising.clearData();
  
  printConnectionStats();
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (_isDeviceConnected && len > 0) {
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

// Reduced write interval for better responsiveness with iOS
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
  
  // Send queued data first
  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    
    // Use write with response for better reliability with iOS
    if (bleuart.write(send_queue[0].buf, send_queue[0].len)) {
      BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);
    } else {
      BLE_DEBUG_PRINTLN("writeBytes failed, may be disconnected");
      // Don't remove from queue if write failed, will retry
      return 0;
    }

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }
  
  // Check for received data
  int len = bleuart.available();
  if (len > 0) {
    if (len > MAX_FRAME_SIZE) {
      BLE_DEBUG_PRINTLN("Received frame too large: %d, truncating", len);
      len = MAX_FRAME_SIZE;
    }
    
    len = bleuart.readBytes(dest, len);  // Read actual bytes received
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", len, (uint32_t) dest[0]);
    return len;
  }
  
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return _isDeviceConnected;
}

void SerialBLEInterface::printConnectionStats() {
  BLE_DEBUG_PRINTLN("=== BLE Connection Statistics ===");
  BLE_DEBUG_PRINTLN("Total connections: %u", conn_stats.total_connections);
  BLE_DEBUG_PRINTLN("Failed connections: %u", conn_stats.failed_connections);
  BLE_DEBUG_PRINTLN("Disconnections: %u", conn_stats.disconnections);
  BLE_DEBUG_PRINTLN("Timeouts: %u", conn_stats.timeouts);
  BLE_DEBUG_PRINTLN("Consecutive failures: %u", conn_stats.consecutive_failures);
  if (conn_stats.total_connections > 0) {
    float success_rate = 100.0f * (conn_stats.total_connections - conn_stats.failed_connections) / conn_stats.total_connections;
    BLE_DEBUG_PRINTLN("Success rate: %.1f%%", success_rate);
  }
  BLE_DEBUG_PRINTLN("================================");
}

bool SerialBLEInterface::isConnectionStable() const {
  if (!_isDeviceConnected) return false;
  
  unsigned long connection_duration = millis() - last_connection_time;
  return connection_duration >= CONNECTION_STABILITY_TIME && conn_stats.consecutive_failures < 3;
}
