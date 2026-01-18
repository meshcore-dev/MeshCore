#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#if defined(NRF52_PLATFORM)

class NRF52Board : public mesh::MainBoard {
public:
  float getMCUTemperature() override;
  uint32_t getIRQGpio() override;
  bool safeToSleep() override;
  void sleep(uint32_t secs) override;
};
#endif