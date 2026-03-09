#pragma once

#include "../BaseSerialInterface.h"
#antml:parameter name <bluefruit.h>

#ifndef BLE_TX_POWER
#define BLE_TX_POWER 4
#endif

// Forward declaration
class SerialBLEInterfaceWithOTA;

// BLE Service for OTA trigger
class BLEOTATriggerService : public BLEService {
private:
  BLECharacteristic _ota_char;
  SerialBLEInterfaceWithOTA* _parent;
  
  static void ota_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);

public:
  BLEOTATriggerService();
  void begin(SerialBLEInterfaceWithOTA* parent);
};

class SerialBLEInterfaceWithOTA : public BaseSerialInterface {
  BLEUart bleuart;
  BLEOTATriggerService ota_service;
  bool _isEnabled;
  bool _isDeviceConnected;
  uint16_t _conn_handle;
  unsigned long _last_health_check;
  unsigned long _last_retry_attempt;
  bool _ota_requested;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  12
  
  uint8_t send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];
  
  uint8_t recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];

  void clearBuffers();
  void shiftSendQueueLeft();
  void shiftRecvQueueLeft();
  bool isValidConnection(uint16_t handle, bool requireWaitingForSecurity = false) const;
  bool isAdvertising() const;
  static void onConnect(uint16_t connection_handle);
  static void onDisconnect(uint16_t connection_handle, uint8_t reason);
  static void onSecured(uint16_t connection_handle);
  static bool onPairingPasskey(uint16_t connection_handle, uint8_t const passkey[6], bool match_request);
  static void onPairingComplete(uint16_t connection_handle, uint8_t auth_status);
  static void onBLEEvent(ble_evt_t* evt);
  static void onBleUartRX(uint16_t conn_handle);

public:
  SerialBLEInterfaceWithOTA() {
    _isEnabled = false;
    _isDeviceConnected = false;
    _conn_handle = BLE_CONN_HANDLE_INVALID;
    _last_health_check = 0;
    _last_retry_attempt = 0;
    _ota_requested = false;
    send_queue_len = 0;
    recv_queue_len = 0;
  }

  void begin(const char* prefix, char* name, uint32_t pin_code);
  void sendFrame(const uint8_t* frame, uint8_t len) override;
  bool receiveFrame(uint8_t* frame, uint8_t* len) override;
  bool isConnected() const override;
  void disconnect();
  void checkHealth();
  
  // OTA trigger method
  void triggerOTA();
  bool isOTARequested() const { return _ota_requested; }
  
  friend class BLEOTATriggerService;
};
