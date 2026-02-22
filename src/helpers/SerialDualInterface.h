#pragma once

#include "ArduinoSerialInterface.h"

template <typename BLEInterface>
class SerialDualInterface : public BaseSerialInterface {
  BLEInterface _ble;
  ArduinoSerialInterface _usb;
  bool _bleWasConnected;

public:
  SerialDualInterface() : _bleWasConnected(false) { }

  void begin(const char* prefix, char* name, uint32_t pin_code, Stream& serial) {
    _ble.begin(prefix, name, pin_code);
    _usb.begin(serial);
  }

  void enable() override {
    _ble.enable();
    _usb.enable();
  }

  void disable() override {
    _ble.disable();
    _usb.disable();
  }

  bool isEnabled() const override {
    return _ble.isEnabled() || _usb.isEnabled();
  }

  bool isConnected() const override {
    return _ble.isConnected() || _usb.isConnected();
  }

  bool isWriteBusy() const override {
    if (_ble.isConnected()) return _ble.isWriteBusy();
    return _usb.isWriteBusy();
  }

  size_t writeFrame(const uint8_t src[], size_t len) override {
    if (_ble.isConnected()) return _ble.writeFrame(src, len);
    return _usb.writeFrame(src, len);
  }

  size_t checkRecvFrame(uint8_t dest[]) override {
    // Always call BLE first, it needs polling for send queue draining
    // and advertising watchdog, even when USB is the active transport
    size_t n = _ble.checkRecvFrame(dest);

    bool bleNow = _ble.isConnected();
    if (bleNow != _bleWasConnected) {
      if (bleNow) {
        // BLE just connected, wait for any pending USB write to finish
        while (_usb.isWriteBusy()) { }
        _usb.disable();
      } else {
        // BLE just disconnected, re-enable USB (resets state machine,
        // discarding any stale bytes from the previous session)
        _usb.enable();
      }
      _bleWasConnected = bleNow;
    }

    if (n > 0) return n;  // got a BLE frame

    // Only check USB when BLE is not connected
    if (!bleNow) {
      return _usb.checkRecvFrame(dest);
    }
    return 0;
  }
};
