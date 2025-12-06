#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#if defined(NRF52_PLATFORM)

class NRF52Board : public mesh::MainBoard {
public:
  float getMCUTemperature() override;
};

#endif