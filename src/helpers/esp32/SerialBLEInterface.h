#pragma once

#include "../SerialBLECommon.h"
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLECharacteristic.h>

class SerialBLEInterface : public SerialBLEInterfaceBase,
                           public NimBLEServerCallbacks,
                           public NimBLECharacteristicCallbacks {
  NimBLEServer* pServer;
  NimBLEService* pService;
  NimBLECharacteristic* pTxCharacteristic;
  NimBLECharacteristic* pRxCharacteristic;

  void clearBuffers();
  bool isValidConnection(uint16_t conn_handle, bool requireWaitingForSecurity = false) const;
  bool isAdvertising() const;
  void requestSyncModeConnection();
  void requestDefaultConnection();

  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
  void onConnParamsUpdate(NimBLEConnInfo& connInfo) override;
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;

public:
  SerialBLEInterface() {
    pServer = nullptr;
    pService = nullptr;
    pTxCharacteristic = nullptr;
    pRxCharacteristic = nullptr;
    initCommonState();
  }

  void begin(const char* device_name, uint32_t pin_code);
  void disconnect();
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }
  bool isConnected() const override;
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};
