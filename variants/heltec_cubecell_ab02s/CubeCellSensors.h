#pragma once
#include <helpers/SensorManager.h>

// GPS settings exposed through MeshCore's custom-variable commands (CMD_GET/SET_CUSTOM_VAR); read-only keys give
// the hardware tests visibility into GPS and OLED state.
class CubeCellSensors : public SensorManager {
  char _val[12];
  const char* _oled;
public:
  CubeCellSensors() : _oled("none") { _val[0] = 0; }
  void setOledStatus(const char* s) { _oled = s; }
  bool begin() override;
  void loop() override;
  int getNumSettings() const override { return 7; }
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
