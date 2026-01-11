#pragma once

#include "../SerialBLECommon.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

class SerialBLEInterface : public SerialBLEInterfaceBase,
                           public BLESecurityCallbacks,
                           public BLEServerCallbacks,
                           public BLECharacteristicCallbacks {
  BLEServer* pServer;
  BLEService* pService;
  BLECharacteristic* pTxCharacteristic;
  uint32_t _pin_code;
  esp_bd_addr_t _peer_addr;  // Bluedroid needs peer address for updateConnParams
  bool _notify_failed;  // Flag set by onStatus callback to track notify failures
  bool _isAdvertising;  // Track advertising state via GAP events (Bluedroid has no isAdvertising() method)

  static SerialBLEInterface* instance;

  void clearBuffers();
  bool isValidConnection(uint16_t conn_id, bool requireWaitingForSecurity = false) const;
  bool isAdvertising() const;
  void requestSyncModeConnection();
  void requestDefaultConnection();
  void onConnParamsUpdate(esp_ble_gap_cb_param_t* param);
  void onAdvStartComplete(esp_ble_gap_cb_param_t* param);
  void onAdvStopComplete(esp_ble_gap_cb_param_t* param);

  static void gapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);

protected:
  // BLESecurityCallbacks methods
  uint32_t onPassKeyRequest() override;
  void onPassKeyNotify(uint32_t pass_key) override;
  bool onConfirmPIN(uint32_t pass_key) override;
  bool onSecurityRequest() override;
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;

  // BLEServerCallbacks methods
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override;
  void onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override;

  // BLECharacteristicCallbacks methods
  void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) override;
  void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) override;

public:
  SerialBLEInterface() {
    pServer = nullptr;
    pService = nullptr;
    pTxCharacteristic = nullptr;
    _pin_code = 0;
    memset(_peer_addr, 0, sizeof(_peer_addr));
    _notify_failed = false;
    _isAdvertising = false;
    initCommonState();
  }

  void begin(const char* device_name, uint32_t pin_code);
  void disconnect();

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }
  bool isConnected() const override;
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};
