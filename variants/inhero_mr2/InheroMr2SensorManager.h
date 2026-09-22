/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <helpers/sensors/EnvironmentSensorManager.h>

class BoardConfigContainer;

class InheroMr2SensorManager : public EnvironmentSensorManager {
  BoardConfigContainer* _board_config = nullptr;

public:
  using EnvironmentSensorManager::EnvironmentSensorManager;

  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;

  // The board owns and initializes the telemetry source before sensor setup.
  void setBoardTelemetrySource(BoardConfigContainer& config) { _board_config = &config; }

  // NAN keeps station pressure and the pressure-derived altitude.
  static void setBme280StationAltitude(float altitude_m);
  static float getBme280StationAltitude();
  static float pressureToQnh(float pressure_hpa, float altitude_m);
};
