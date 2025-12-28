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
  void trySendNextFrame();
  void onTxComplete(uint8_t count);

  // Event-driven TX state
  uint8_t _tx_pending;    // Notifications in-flight (for debugging/stats only)
  bool _tx_in_progress;   // Guard against re-entrant trySendNextFrame()

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
    _tx_pending = 0;
    _tx_in_progress = false;
  }

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
