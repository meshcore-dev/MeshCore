#pragma once

#include <Arduino.h>

#define MAX_FRAME_SIZE  176   // +4 for transport codes (region scoping)

class BaseSerialInterface {
protected:
  BaseSerialInterface() { }

public:
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual bool isEnabled() const = 0;

  virtual bool isConnected() const = 0;

  // Notification eligibility, as opposed to isConnected()'s "a client is
  // actively talking to us". Defaults to isConnected(); transports whose
  // isConnected() additionally demands RECENT traffic (USB-CDC without DTR)
  // override it, so a client that only listens keeps getting push frames.
  virtual bool isSessionEstablished() const { return isConnected(); }

  virtual void loop() {};

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;
};
