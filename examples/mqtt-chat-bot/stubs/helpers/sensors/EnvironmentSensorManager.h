#pragma once

#include <helpers/SensorManager.h>

class EnvironmentSensorManager : public SensorManager {
 public:
  EnvironmentSensorManager() = default;
  explicit EnvironmentSensorManager(LocationProvider& /*location*/) {}

  bool begin() override { return false; }
  bool querySensors(uint8_t /*requester_permissions*/, CayenneLPP& /*telemetry*/) override {
    return false;
  }
  void loop() override {}
  int getNumSettings() const override { return 0; }
  const char* getSettingName(int /*i*/) const override { return nullptr; }
  const char* getSettingValue(int /*i*/) const override { return nullptr; }
  bool setSettingValue(const char* /*name*/, const char* /*value*/) override { return false; }
};
