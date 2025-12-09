#pragma once

#include <Arduino.h>
#include <MeshCore.h>

class NRF52Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  virtual void begin() { startup_reason = BD_STARTUP_NORMAL; }

  virtual uint8_t getStartupReason() const override { return startup_reason; }

  virtual void reboot() override { NVIC_SystemReset(); }
};