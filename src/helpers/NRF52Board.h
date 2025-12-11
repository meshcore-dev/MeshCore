#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#if defined(NRF52_PLATFORM)

class NRF52Board : public mesh::MainBoard {
public:
  float getMCUTemperature() override;

  void enterLightSleep(uint32_t secs);
  void sleep(uint32_t secs) override;
};
#endif