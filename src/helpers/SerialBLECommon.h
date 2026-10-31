#pragma once

#include "BaseSerialInterface.h"
#include <stdio.h>
#include <string.h>

// Nordic UART service
#define BLE_UART_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_UART_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_UART_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Units: interval=1.25ms, timeout=10ms
#define BLE_MIN_CONN_INTERVAL      12
#define BLE_MAX_CONN_INTERVAL      36
#define BLE_SLAVE_LATENCY          3
#define BLE_CONN_SUP_TIMEOUT       500

// Units: advertising interval=0.625ms
#define BLE_ADV_INTERVAL_MIN       32
#define BLE_ADV_INTERVAL_MAX       338
#define BLE_ADV_FAST_TIMEOUT       30   // seconds

#define BLE_MAX_MTU                247   // nRF52 max
#define BLE_DLE_MAX_TX_OCTETS      251

#define BLE_FRAME_QUEUE_SIZE       2048   // bytes: frames are packed by size, so a burst of small commands fits

// name is IN/OUT: "@@MAC" is replaced by the address in hex, printed addr[0] first (or addr[5] first if reversed)
inline void makeBLEName(char* dev_name, size_t size, const char* prefix, char* name, const uint8_t addr[6], bool reversed) {
  if (strcmp(name, "@@MAC") == 0) {
    for (int i = 0; i < 6; i++) {
      sprintf(&name[i * 2], "%02X", addr[reversed ? 5 - i : i]);
    }
  }
  snprintf(dev_name, size, "%s%s", prefix, name);
}

struct SerialBLEFrame {
  uint8_t len;
  const uint8_t* buf;
};

// single producer / single consumer ring, so the BLE task may push while the main loop pops (and clears).
// Frames are stored as [len][bytes] and never wrap, so peek() hands out one contiguous buffer.
template <uint16_t N>
class SerialBLEFrameQueue {
  static_assert(N > 2 * (MAX_FRAME_SIZE + 1), "queue too small");   // any frame fits once empty
  static const uint8_t WRAP = 0xFF;   // as a length: rest of the buffer unused, continue at 0
  uint8_t _buf[N];
  volatile uint16_t _head = 0, _tail = 0;   // read and write offsets, equal when empty

public:
  void clear() { _head = _tail; }
  bool isEmpty() const { return _head == _tail; }
  uint16_t used() const { uint16_t h = _head, t = _tail; return t >= h ? t - h : N - h + t; }

  // caller ensures len <= MAX_FRAME_SIZE
  bool push(const uint8_t* src, size_t len) {
    uint16_t h = _head, t = _tail, need = len + 1, at = t;
    if (t < h) {
      if (need >= h - t) return false;   // a full ring must not look empty
    } else if (N - t < need + (h == 0)) {
      if (need >= h) return false;
      at = 0;
    }
    _buf[at] = len;
    memcpy(&_buf[at + 1], src, len);
    if (at != t) _buf[t] = WRAP;
    __sync_synchronize();
    _tail = (at + need) % N;
    return true;
  }
  SerialBLEFrame peek() const {
    uint16_t h = _head;
    if (_buf[h] == WRAP) h = 0;
    return { _buf[h], &_buf[h + 1] };
  }
  void pop() {
    uint16_t h = _head;
    if (h == _tail) return;
    if (_buf[h] == WRAP) h = 0;
    h = (h + 1 + _buf[h]) % N;
    __sync_synchronize();
    _head = h;
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
