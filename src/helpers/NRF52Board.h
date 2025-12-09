#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#if defined(NRF52_PLATFORM)

class NRF52Board : public mesh::MainBoard {
public:
  float getMCUTemperature() override;
  virtual void reboot() override { NVIC_SystemReset(); }
};
#endif