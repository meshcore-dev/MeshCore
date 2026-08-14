#pragma once

#include "BaseSerialInterface.h"
#include <Arduino.h>

class ArduinoSerialInterface : public BaseSerialInterface {
public:
  // reports whether a client currently has this stream open (see setConnectedCheck)
  typedef bool (*ConnectedCheck)();

private:
  bool _isEnabled;
  uint8_t _state;
  uint16_t _frame_len;
  uint16_t rx_len;
  uint32_t _last_frame_ms;
  Stream* _serial;
  ConnectedCheck _conn_check;
  uint8_t rx_buf[MAX_FRAME_SIZE];

public:
  ArduinoSerialInterface() { _isEnabled = false; _state = 0; _last_frame_ms = 0; _conn_check = NULL; }

  void begin(Stream& serial) {
    _serial = &serial;
  #ifdef RAK_4631
    pinMode(WB_IO2, OUTPUT);
  #endif
  }

  // Optional: let the target report the real link state, e.g. USB-CDC DTR.
  // Without it isConnected() assumes true, as a plain UART has no way of knowing.
  void setConnectedCheck(ConnectedCheck fn) { _conn_check = fn; }

  // millis() of the last completely received frame, 0 if none since boot.
  // Useful as an activity-based connection check where no DTR state exists.
  uint32_t getLastFrameMillis() const { return _last_frame_ms; }

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};