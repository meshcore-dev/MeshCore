#pragma once

#include "../SerialBLECommon.h"
#include <NimBLEDevice.h>

class SerialBLEInterface : public BaseSerialInterface, NimBLEServerCallbacks, NimBLECharacteristicCallbacks {
  NimBLEServer *pServer;
  NimBLECharacteristic *pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  bool _subscribed;
  bool _mtu_done;
  uint16_t _conn_handle;
  unsigned long _last_retry;
  unsigned long _last_adv_check;

  SerialBLEFrameQueue<BLE_FRAME_QUEUE_SIZE> recv_queue;
  SerialBLEFrameQueue<BLE_FRAME_QUEUE_SIZE> send_queue;

  void clearBuffers();

protected:
  // NimBLEServerCallbacks methods
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override;
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;

  // NimBLECharacteristicCallbacks methods
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override;

public:
  SerialBLEInterface() {
    pServer = NULL;
    pTxCharacteristic = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    _isEnabled = false;
    _subscribed = false;
    _mtu_done = false;
    _last_retry = 0;
    _last_adv_check = 0;
    _conn_handle = BLE_HS_CONN_HANDLE_NONE;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};
