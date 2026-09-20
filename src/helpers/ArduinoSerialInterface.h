#pragma once

#include "BaseSerialInterface.h"
#include <Arduino.h>

class ArduinoSerialInterface : public BaseSerialInterface {
public:
  typedef bool (*ConnectedCheck)();

private:
  bool _isEnabled;
  bool _flow_ctl;
  uint8_t _state;
  uint16_t _frame_len;
  uint16_t rx_len;
  uint32_t _last_frame_ms;
  // high-water mark of availableForWrite(): probes the stream's real TX buffer
  // capacity at runtime (Print's default is 0, USB-CDC reports 0 while nothing
  // is connected -- only the peak ever observed reveals the true ceiling)
  mutable int _max_afw;
  Stream* _serial;
  ConnectedCheck _conn_check;
  ConnectedCheck _estab_check;
  uint8_t rx_buf[MAX_FRAME_SIZE];

public:
  ArduinoSerialInterface() { _isEnabled = false; _flow_ctl = false; _state = 0; _last_frame_ms = 0; _max_afw = 0; _conn_check = NULL; _estab_check = NULL; }

  void begin(Stream& serial) {
    _serial = &serial;
  #ifdef RAK_4631
    pinMode(WB_IO2, OUTPUT);
  #endif
  }

  // optional: lets the target report the real link state (e.g. USB-CDC DTR).
  // Without this, isConnected() assumes true (plain UARTs have no way of knowing).
  void setConnectedCheck(ConnectedCheck fn) { _conn_check = fn; }

  // optional: reports whether a client session was ever established on a link
  // that is still up -- without the recent-traffic requirement isConnected()
  // may carry. Used for push notifications, so an idle listener is not cut off.
  void setEstablishedCheck(ConnectedCheck fn) { _estab_check = fn; }

  // optional: only write a frame when it fits into the stream's TX buffer as a
  // whole (and report busy to pace bulk streams like the contact sync).
  // A partially written frame permanently desyncs the length-prefixed framing
  // (no checksum/resync), so dropping whole frames is strictly better.
  void enableFlowControl(bool enable) { _flow_ctl = enable; }

  // millis() of the last completely received frame, 0 = none since boot/unplug
  uint32_t getLastFrameMillis() const { return _last_frame_ms; }

  // Forget the activity timestamp, so a new session has to prove itself with a
  // fresh frame. Does NOT touch the parser or the queue -- see the definition.
  void resetActivity();

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;
  bool isSessionEstablished() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};