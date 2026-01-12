#pragma once

#include <stdint.h>

class CayenneLPP;

#define TELEM_PERM_BASE 0x01
#define TELEM_PERM_LOCATION 0x02
#define TELEM_PERM_ENVIRONMENT 0x04

#define TELEM_CHANNEL_SELF 1

class LocationProvider;

class SensorManager {
 public:
  double node_lat = 0.0;
  double node_lon = 0.0;
  double node_altitude = 0.0;

  virtual ~SensorManager() = default;
  virtual bool begin() { return false; }
  virtual bool querySensors(uint8_t /*requester_permissions*/, CayenneLPP& /*telemetry*/) {
    return false;
  }
  virtual void loop() {}
  virtual int getNumSettings() const { return 0; }
  virtual const char* getSettingName(int /*i*/) const { return nullptr; }
  virtual const char* getSettingValue(int /*i*/) const { return nullptr; }
  virtual bool setSettingValue(const char* /*name*/, const char* /*value*/) { return false; }
  virtual LocationProvider* getLocationProvider() { return nullptr; }

  const char* getSettingByKey(const char* /*key*/) { return nullptr; }
};
