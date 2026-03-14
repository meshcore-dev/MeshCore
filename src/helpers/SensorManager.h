#pragma once

#include <CayenneLPP.h>
#include "sensors/LocationProvider.h"
#include "helpers/IdentityStore.h"

#define TELEM_PERM_BASE         0x01   // 'base' permission includes battery
#define TELEM_PERM_LOCATION     0x02
#define TELEM_PERM_ENVIRONMENT  0x04   // permission to access environment sensors

#define TELEM_CHANNEL_SELF   1   // LPP data channel for 'self' device

#define MAX_SETTING_VALUE_LEN  7   // max chars returned by getSettingValue (excluding null terminator)
#define MAX_SETTING_BUF_LEN   (MAX_SETTING_VALUE_LEN+1)

class SensorManager {
public:
  double node_lat, node_lon;  // modify these, if you want to affect Advert location
  double node_altitude;       // altitude in meters

  SensorManager() { node_lat = 0; node_lon = 0; node_altitude = 0; }
  virtual bool begin(FILESYSTEM* fs = nullptr) { return false; }
  virtual bool hasPendingWork() { return false; }
  virtual bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) { return false; }
  virtual void loop() { }
  virtual int getNumSettings() const { return 0; }
  virtual const char* getSettingName(int i) const { return NULL; }
  virtual int getSettingValue(int i, char* buf, int bufLen) const { return 0; }
  virtual bool setSettingValue(const char* name, const char* value) { return false; }
  virtual LocationProvider* getLocationProvider() { return NULL; }

  // Helper functions to manage setting by keys (useful in many places ...)
  int getSettingByKey(const char* key, char* buf, int bufLen) {
    int num = getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(getSettingName(i), key) == 0) {
        return getSettingValue(i, buf, bufLen);
      }
    }
    return 0;
  }
};
