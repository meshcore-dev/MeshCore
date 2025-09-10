#pragma once

#include "../BaseSerialInterface.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

class SerialBLEInterface : public BaseSerialInterface, BLESecurityCallbacks, BLEServerCallbacks, BLECharacteristicCallbacks {
  BLEServer *pServer;
  BLEService *pService;
  BLECharacteristic * pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  uint16_t last_conn_id;
  uint32_t _pin_code;
  unsigned long _last_write;
  unsigned long adv_restart_time;
  unsigned long last_connection_time;
  unsigned long connection_supervision_timeout;
  bool connection_params_updated;
  bool ios_device_detected;
  uint8_t connection_retry_count;
  
  // Connection stability tracking
  struct ConnectionStats {
    uint32_t total_connections;
    uint32_t failed_connections;
    uint32_t disconnections;
    uint32_t timeouts;
    unsigned long last_disconnect_time;
    uint8_t consecutive_failures;
  } conn_stats;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  8  // Increased queue size for better reliability
  int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() { recv_queue_len = 0; send_queue_len = 0; }
  void resetConnectionStats();
  void updateConnectionParameters();
  bool shouldRetryConnection();
  void handleConnectionFailure();

protected:
  // BLESecurityCallbacks methods
  uint32_t onPassKeyRequest() override;
  void onPassKeyNotify(uint32_t pass_key) override;
  bool onConfirmPIN(uint32_t pass_key) override;
  bool onSecurityRequest() override;
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;

  // BLEServerCallbacks methods
  void onConnect(BLEServer* pServer) override;
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override;
  void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override;
  void onDisconnect(BLEServer* pServer) override;

  // BLECharacteristicCallbacks methods
  void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) override;

public:
  SerialBLEInterface() {
    pServer = NULL;
    pService = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    adv_restart_time = 0;
    _isEnabled = false;
    _last_write = 0;
    last_conn_id = 0;
    send_queue_len = recv_queue_len = 0;
    last_connection_time = 0;
    connection_supervision_timeout = 0;
    connection_params_updated = false;
    ios_device_detected = false;
    connection_retry_count = 0;
    // Initialize connection stats
    memset(&conn_stats, 0, sizeof(conn_stats));
  }

  void begin(const char* device_name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
  
  // Stability monitoring methods
  void printConnectionStats();
  bool isConnectionStable() const;
};

// iOS-optimized connection parameters (following Apple's formulaic rules)
#define IOS_MIN_CONN_INTERVAL     24   // 30ms (in 1.25ms units) - Apple's minimum multiple of 15ms
#define IOS_MAX_CONN_INTERVAL     40   // 50ms (in 1.25ms units) - satisfies Min + 15ms ≤ Max
#define IOS_SLAVE_LATENCY         0    // No latency for real-time data
#define IOS_CONN_SUP_TIMEOUT      400  // 4 seconds (in 10ms units) - satisfies 2s ≤ timeout ≤ 6s

// Backup iOS connection parameters for power-saving mode
#define IOS_POWER_MIN_CONN_INTERVAL  48   // 60ms (in 1.25ms units) - multiple of 15ms  
#define IOS_POWER_MAX_CONN_INTERVAL  80   // 100ms (in 1.25ms units) - satisfies Min + 15ms ≤ Max
#define IOS_POWER_SLAVE_LATENCY      2    // Allow 2 intervals latency for power saving
#define IOS_POWER_CONN_SUP_TIMEOUT   500  // 5 seconds (in 10ms units)

// Advertising parameters optimized for iOS (Apple's exact recommended intervals)
#define IOS_ADV_FAST_INTERVAL     32   // 20ms (Apple recommended) - EXACT
#define IOS_ADV_SLOW_INTERVAL_1   244  // 152.5ms - Apple's first recommended slow interval
#define IOS_ADV_SLOW_INTERVAL_2   338  // 211.25ms - Apple's second recommended slow interval  
#define IOS_ADV_SLOW_INTERVAL_3   510  // 318.75ms - Apple's third recommended slow interval
#define IOS_ADV_SLOW_INTERVAL_4   668  // 417.5ms - Apple's fourth recommended slow interval
#define IOS_ADV_FAST_TIMEOUT      30   // 30 seconds in fast mode

// Connection retry parameters  
#define MAX_CONNECTION_RETRIES    5
#define CONNECTION_RETRY_DELAY    2000  // 2 seconds between retries
#define CONNECTION_STABILITY_TIME 10000 // 10 seconds to consider connection stable

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
