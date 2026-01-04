#pragma once

#include "BaseSerialInterface.h"
#include <string.h>

// Units: interval=1.25ms, timeout=10ms
#define BLE_MIN_CONN_INTERVAL      12
#define BLE_MAX_CONN_INTERVAL      36
#define BLE_SLAVE_LATENCY          3
#define BLE_CONN_SUP_TIMEOUT       500

// Sync mode: higher throughput (min 15ms for Apple compliance)
#define BLE_SYNC_MIN_CONN_INTERVAL   12
#define BLE_SYNC_MAX_CONN_INTERVAL   24
#define BLE_SYNC_SLAVE_LATENCY       0
#define BLE_SYNC_CONN_SUP_TIMEOUT    300

#define BLE_SYNC_INACTIVITY_TIMEOUT_MS  5000

// Units: advertising interval=0.625ms
// ESP randomly chooses between 32 and 338
// max seems slow, but we can wait a few seconds for it to connect, worth the battery
#define BLE_ADV_INTERVAL_MIN       32
#define BLE_ADV_INTERVAL_MAX       338
#define BLE_ADV_FAST_TIMEOUT       30

#define BLE_HEALTH_CHECK_INTERVAL  10000
#define BLE_RETRY_THROTTLE_MS      250
#define BLE_MIN_SEND_INTERVAL_MS   8
#define BLE_RX_DRAIN_BUF_SIZE      32

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define BLE_SYNC_FRAME_SIZE_THRESHOLD  40
#define BLE_SYNC_LARGE_FRAME_COUNT_THRESHOLD  3
#define BLE_SYNC_LARGE_FRAME_WINDOW_MS  1500

#define BLE_CONN_HANDLE_INVALID  0xFFFF

// BLE specific MTU target, ESP can do more, but we don't need it, so stay at max nRF52
#define BLE_MAX_MTU              247

// ESP needs this to set manually, nRF52 handles it automatically
#define BLE_DLE_MAX_TX_OCTETS    251
#define BLE_DLE_MAX_TX_TIME_US   2120

// nRF only, NimBLE cannot set TX power on ESP, so ESP is fixed 0dBm
#ifndef BLE_TX_POWER
#define BLE_TX_POWER 4
#endif

struct SerialBLEFrame {
  uint8_t len;
  uint8_t buf[MAX_FRAME_SIZE];
};

// 12 is adequate for most use cases
#define FRAME_QUEUE_SIZE  12

struct CircularFrameQueue {
  SerialBLEFrame frames[FRAME_QUEUE_SIZE];
  uint8_t head;
  uint8_t tail;
  uint8_t count;

  void init() {
    head = 0;
    tail = 0;
    count = 0;
  }

  bool isEmpty() const {
    return count == 0;
  }

  bool isFull() const {
    return count >= FRAME_QUEUE_SIZE;
  }

  SerialBLEFrame* peekFront() {
    if (isEmpty()) return nullptr;
    return &frames[tail];
  }

  SerialBLEFrame* getWriteSlot() {
    if (isFull()) return nullptr;
    return &frames[head];
  }

  void push() {
    if (!isFull()) {
      head = (head + 1) % FRAME_QUEUE_SIZE;
      count++;
    }
  }

  void pop() {
    if (!isEmpty()) {
      tail = (tail + 1) % FRAME_QUEUE_SIZE;
      count--;
    }
  }

  uint8_t size() const {
    return count;
  }
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif

class SerialBLEInterfaceBase : public BaseSerialInterface {
protected:
  bool _isEnabled;
  bool _isDeviceConnected;
  uint16_t _conn_handle;
  unsigned long _last_health_check;
  unsigned long _last_retry_attempt;
  unsigned long _last_send_time;
  unsigned long _last_activity_time;
  bool _sync_mode;
  bool _conn_param_update_pending;
  uint8_t _large_frame_count;
  unsigned long _large_frame_window_start;

  CircularFrameQueue send_queue;
  CircularFrameQueue recv_queue;

  void clearTransferState() {
    send_queue.init();
    recv_queue.init();
    _last_retry_attempt = 0;
    _last_send_time = 0;
    _last_activity_time = 0;
    _sync_mode = false;
    _conn_param_update_pending = false;
    _large_frame_count = 0;
    _large_frame_window_start = 0;
  }

  void popSendQueue() {
    send_queue.pop();
  }

  void popRecvQueue() {
    recv_queue.pop();
  }

  bool noteFrameActivity(unsigned long now, size_t frame_len) {
    if (frame_len < BLE_SYNC_FRAME_SIZE_THRESHOLD) {
      return false;
    }

    _last_activity_time = now;

    if (_large_frame_window_start == 0 ||
        (now - _large_frame_window_start) > BLE_SYNC_LARGE_FRAME_WINDOW_MS) {
      _large_frame_count = 1;
      _large_frame_window_start = now;
    } else if (_large_frame_count < 255) {
      _large_frame_count++;
    }

    return (!_sync_mode && _large_frame_count >= BLE_SYNC_LARGE_FRAME_COUNT_THRESHOLD);
  }

  bool isWriteBusyCommon() const {
    return send_queue.size() >= (FRAME_QUEUE_SIZE * 2 / 3);
  }

  void initCommonState() {
    _isEnabled = false;
    _isDeviceConnected = false;
    _conn_handle = BLE_CONN_HANDLE_INVALID;
    _last_health_check = 0;
    _last_retry_attempt = 0;
    _last_send_time = 0;
    _last_activity_time = 0;
    _sync_mode = false;
    _conn_param_update_pending = false;
    _large_frame_count = 0;
    _large_frame_window_start = 0;
    send_queue.init();
    recv_queue.init();
  }
};

