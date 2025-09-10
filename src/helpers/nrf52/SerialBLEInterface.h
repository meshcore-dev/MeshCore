#pragma once

#include "../BaseSerialInterface.h"
#include <bluefruit.h>

class SerialBLEInterface : public BaseSerialInterface {
  BLEUart bleuart;
  bool _isEnabled;
  
protected:
  bool _isDeviceConnected;
  
private:
  unsigned long _last_write;
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
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() { send_queue_len = 0; }
  void resetConnectionStats();
  void updateConnectionParameters();
  bool shouldRetryConnection();
  void handleConnectionFailure();
  
  static void onConnect(uint16_t connection_handle);
  static void onDisconnect(uint16_t connection_handle, uint8_t reason);

public:
  SerialBLEInterface() {
    _isEnabled = false;
    _isDeviceConnected = false;
    _last_write = 0;
    send_queue_len = 0;
    last_connection_time = 0;
    connection_supervision_timeout = 0;
    connection_params_updated = false;
    ios_device_detected = false;
    connection_retry_count = 0;
    // Initialize connection stats
    memset(&conn_stats, 0, sizeof(conn_stats));
  }

  void startAdv();
  void stopAdv();
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

// iOS-optimized connection parameters for NRF52 (following Apple's formulaic rules)
#define NRF_IOS_MIN_CONN_INTERVAL     24   // 30ms (in 1.25ms units) - Apple's minimum multiple of 15ms
#define NRF_IOS_MAX_CONN_INTERVAL     40   // 50ms (in 1.25ms units) - satisfies Min + 15ms ≤ Max
#define NRF_IOS_SLAVE_LATENCY         0    // No latency for real-time data
#define NRF_IOS_CONN_SUP_TIMEOUT      400  // 4 seconds (in 10ms units) - satisfies 2s ≤ timeout ≤ 6s

// Backup iOS connection parameters for power-saving mode
#define NRF_IOS_POWER_MIN_CONN_INTERVAL  48   // 60ms (in 1.25ms units) - multiple of 15ms
#define NRF_IOS_POWER_MAX_CONN_INTERVAL  80   // 100ms (in 1.25ms units) - satisfies Min + 15ms ≤ Max
#define NRF_IOS_POWER_SLAVE_LATENCY      2    // Allow 2 intervals latency for power saving
#define NRF_IOS_POWER_CONN_SUP_TIMEOUT   500  // 5 seconds (in 10ms units)

// Advertising parameters optimized for iOS (Apple's exact recommended intervals)
#define NRF_IOS_ADV_FAST_INTERVAL     32   // 20ms (Apple recommended) - EXACT
#define NRF_IOS_ADV_SLOW_INTERVAL_1   244  // 152.5ms - Apple's first recommended slow interval
#define NRF_IOS_ADV_SLOW_INTERVAL_2   338  // 211.25ms - Apple's second recommended slow interval
#define NRF_IOS_ADV_SLOW_INTERVAL_3   510  // 318.75ms - Apple's third recommended slow interval
#define NRF_IOS_ADV_SLOW_INTERVAL_4   668  // 417.5ms - Apple's fourth recommended slow interval
#define NRF_IOS_ADV_FAST_TIMEOUT      30   // 30 seconds in fast mode

// Connection retry parameters
#define MAX_CONNECTION_RETRIES        5
#define CONNECTION_RETRY_DELAY        2000  // 2 seconds between retries
#define CONNECTION_STABILITY_TIME     10000 // 10 seconds to consider connection stable

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
