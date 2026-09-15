#pragma once

#include <cstdint>
#include "CayenneLPP.h"

class SensorManager {
public:
  virtual ~SensorManager() = default;
  virtual bool querySensors(uint8_t, CayenneLPP&) { return false; }
};
