#pragma once

#include <Arduino.h>

#define MAX_FRAME_SIZE  172

class LEDManager;

class BaseSerialInterface {
protected:
  BaseSerialInterface() { }
  LEDManager* _ledManager = nullptr;

public:
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual bool isEnabled() const = 0;

  virtual bool isConnected() const = 0;

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;

  void setLEDManager(LEDManager* mgr) { _ledManager = mgr; }
};
