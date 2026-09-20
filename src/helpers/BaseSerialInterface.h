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
  virtual void loop() {};

  virtual bool isWriteBusy() const = 0;
  // Returns len once the transport has taken the frame, and 0 only when
  // nothing of it was written, so that a caller may safely offer the same
  // frame again. A frame the transport tore (a short write on a serial link)
  // counts as taken: a retry cannot mend it, the receiver would swallow the
  // next header as the missing payload.
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;
};
