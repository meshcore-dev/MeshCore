#pragma once

#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include "HeltecV3Board.h"

#ifdef DISPLAY_CLASS
  #include <helpers/ui/DisplayDriver.h>
#endif

class HeltecV3SensorManager : public SensorManager {
  HeltecV3Board* _board;
  EnvironmentSensorManager* _env_sensors;
  #ifdef DISPLAY_CLASS
    DisplayDriver* _display;
  #endif

public:
  HeltecV3SensorManager(HeltecV3Board& board) : _board(&board), _env_sensors(nullptr) {
    #ifdef DISPLAY_CLASS
      _display = nullptr;
    #endif
  }

  HeltecV3SensorManager(HeltecV3Board& board, EnvironmentSensorManager* env) 
      : _board(&board), _env_sensors(env) {
    #ifdef DISPLAY_CLASS
      _display = nullptr;
    #endif
  }

  #ifdef DISPLAY_CLASS
  void setDisplay(DisplayDriver* display) {
    _display = display;
    if (_display) {
      if (_board->getDisplayEnabled()) {
        _display->turnOn();
      } else {
        _display->turnOff();
      }
      if (_board->supportsDisplayBrightness()) {
        _display->setBrightness(_board->getDisplayBrightness());
      }
    }
  }
  #endif

  bool begin() override {
    if (_env_sensors) {
      return _env_sensors->begin();
    }
    return true;
  }

  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override {
    if (_env_sensors) {
      return _env_sensors->querySensors(requester_permissions, telemetry);
    }
    return false;
  }

  void loop() override {
    if (_env_sensors) {
      _env_sensors->loop();
    }
  }

  LocationProvider* getLocationProvider() override {
    if (_env_sensors) {
      return _env_sensors->getLocationProvider();
    }
    return nullptr;
  }

  int getNumSettings() const override {
    int base_count = _env_sensors ? _env_sensors->getNumSettings() : 0;
    return base_count + _board->getNumSettings();
  }

  const char* getSettingName(int i) const override {
    int base_count = _env_sensors ? _env_sensors->getNumSettings() : 0;
    if (i < base_count && _env_sensors) {
      return _env_sensors->getSettingName(i);
    }
    return _board->getSettingName(i - base_count);
  }

  const char* getSettingValue(int i) const override {
    int base_count = _env_sensors ? _env_sensors->getNumSettings() : 0;
    if (i < base_count && _env_sensors) {
      return _env_sensors->getSettingValue(i);
    }
    return _board->getSettingValue(i - base_count);
  }

  bool setSettingValue(const char* name, const char* value) override {
    // Try environment sensors first
    if (_env_sensors && _env_sensors->setSettingValue(name, value)) {
      return true;
    }
    // Then try board settings
    return _board->setSettingValue(name, value);
  }

  const char* getSettingByKey(const char* name) const {
    if (_env_sensors) {
      const char* result = _env_sensors->getSettingByKey(name);
      if (result) return result;
    }
    
    // Check board settings
    for (int i = 0; i < _board->getNumSettings(); i++) {
      const char* setting_name = _board->getSettingName(i);
      if (setting_name && strcmp(setting_name, name) == 0) {
        return _board->getSettingValue(i);
      }
    }
    return nullptr;
  }
};
