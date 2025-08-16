#pragma once

#include <helpers/SensorManager.h>
#include "MeshimiBoard.h"

class MeshimiSensorManager : public SensorManager {
public:
  bool begin() override { return true; }
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
};