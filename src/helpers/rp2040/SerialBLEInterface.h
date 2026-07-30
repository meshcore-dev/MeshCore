#pragma once

#include "../BaseSerialInterface.h"
#include <BLE.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>

class SerialBLEInterface : public BaseSerialInterface, public BLEServerCallbacks, public BLECharacteristicCallbacks {
  BLEServer* _server = nullptr;
  BLEService* _service = nullptr;
  BLECharacteristic* _txChar = new BLECharacteristic(String(TX_UUID), BLERead | BLENotify);
  BLECharacteristic* _rxChar = _rxChar = new BLECharacteristic(String(RX_UUID), BLEWrite | BLEWriteWithoutResponse);
  bool _isEnabled = false;
  bool _isConnected = false;
  unsigned long _last_health_check = 0;
  unsigned long _last_write = 0;

  static constexpr const char* SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
  static constexpr const char* TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
  static constexpr const char* RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE 12
  #define BLE_WRITE_MIN_INTERVAL 60

  uint8_t send_queue_len = 0;
  Frame send_queue[FRAME_QUEUE_SIZE];

  uint8_t recv_queue_len = 0;
  Frame recv_queue[FRAME_QUEUE_SIZE];

  void shiftSendQueueLeft();
  void shiftRecvQueueLeft();

public:
  SerialBLEInterface() {}

  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BLEServerCallbacks
  void onConnect(BLEServer* p) override;
  void onDisconnect(BLEServer* p) override;

  // BLECharacteristicCallbacks - called when app writes to RX characteristic
  void onWrite(BLECharacteristic* c) override;

  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }
  bool isConnected() const override { return _isConnected; }
  bool isWriteBusy() const override { return millis() < _last_write + BLE_WRITE_MIN_INTERVAL; }
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINTLN(...) {}
#endif