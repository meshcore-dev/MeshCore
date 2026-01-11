#pragma once

#include "../SerialBLECommon.h"
#include <bluefruit.h>

class SerialBLEInterface : public SerialBLEInterfaceBase {
  BLEUart bleuart;

  static SerialBLEInterface* instance;

  void clearBuffers();
  bool isValidConnection(uint16_t handle, bool requireWaitingForSecurity = false) const;
  bool isAdvertising() const;
  void requestSyncModeConnection();
  void requestDefaultConnection();

  static void onConnect(uint16_t connection_handle);
  static void onDisconnect(uint16_t connection_handle, uint8_t reason);
  static void onSecured(uint16_t connection_handle);
  static bool onPairingPasskey(uint16_t connection_handle, uint8_t const passkey[6], bool match_request);
  static void onPairingComplete(uint16_t connection_handle, uint8_t auth_status);
  static void onBLEEvent(ble_evt_t* evt);
  static void onBleUartRX(uint16_t conn_handle);

public:
  SerialBLEInterface() {
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
