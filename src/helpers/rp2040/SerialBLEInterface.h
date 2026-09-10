#pragma once

#include "../BaseSerialInterface.h"
#include <BLE.h>
#include <btstack.h>

// Nordic UART service over the arduino-pico BLE library (BTstack on the CYW43).
// Build with -D PIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH so the core links liblwip-bt.
class SerialBLEInterface : public BaseSerialInterface, BLEService, BLEServerCallbacks, BLECharacteristicCallbacks {
  // subclass only to reach the protected connection/notify state
  struct Characteristic : public BLECharacteristic {
    using BLECharacteristic::BLECharacteristic;
    uint16_t valueHandle() const { return _valueHandle; }
    uint16_t conHandle() const { return con_handle; }
    bool notifyEnabled() const { return _notificationEnabled; }
  };

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  8

  Characteristic _rx;
  Characteristic _tx;
  bool _isEnabled;
  bool _tx_pending;   // a can-send-now callback is registered
  btstack_context_callback_registration_t _can_send;
  uint8_t _scan_rsp[31];   // complete local name; BTstack keeps the pointer

  uint8_t send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];
  uint8_t recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];

  void clearBuffers();
  void kickSend();
  void sendNext();
  static void onCanSend(void* ctx) { ((SerialBLEInterface*)ctx)->sendNext(); }

  // BLE library callbacks (run in the BT context)
  void onWrite(BLECharacteristic* c) override;
  void onConnect(BLEServer* s) override;
  void onDisconnect(BLEServer* s) override;

public:
  SerialBLEInterface();

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  void disconnect();
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }
  bool isConnected() const override;
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
