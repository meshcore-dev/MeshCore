/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <helpers/sensors/EnvironmentSensorManager.h>

class InheroMr2SensorManager : public EnvironmentSensorManager {
public:
  using EnvironmentSensorManager::EnvironmentSensorManager;

  bool begin() override;

  // NAN keeps station pressure and the pressure-derived altitude.
  static void setBme280StationAltitude(float altitude_m);
  static float getBme280StationAltitude();
  static float pressureToQnh(float pressure_hpa, float altitude_m);
};
