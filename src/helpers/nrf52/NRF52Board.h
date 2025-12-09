#pragma once

#include <Arduino.h>
#include <MeshCore.h>

class NRF52Board : public mesh::MainBoard {
public:
  virtual void reboot() override { NVIC_SystemReset(); }
};