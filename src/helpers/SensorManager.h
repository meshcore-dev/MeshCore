#pragma once

#include <CayenneLPP.h>
#if ENV_INCLUDE_GPS
#include <helpers/sensors/LocationProvider.h>
#endif

#define TELEM_PERM_BASE         0x01   // 'base' permission includes battery
#define TELEM_PERM_LOCATION     0x02
#define TELEM_PERM_ENVIRONMENT  0x04   // permission to access environment sensors

#define TELEM_CHANNEL_SELF   1   // LPP data channel for 'self' device

class SensorManager {
protected:
  #if ENV_INCLUDE_GPS
  LocationProvider* _location = nullptr;
  LocationProvider* _location_candidates[4];
  int _num_location_candidates = 0;
  bool gps_active = false;
  uint32_t gps_update_interval_sec = 1;
  uint32_t _next_gps_update = 0;

  void detectLocationProvider() {
    for (int i = 0; i < _num_location_candidates; i++) {
      if (_location_candidates[i]->detect()) {
        _location = _location_candidates[i];
        return;
      }
    }
  }

  void updateGpsCoordinates() {
    if (!_location) return;
    if (gps_active) { _location->loop(); }
    if (millis() > _next_gps_update) {
      if (gps_active && _location->isValid()) {
        node_lat = ((double)_location->getLatitude())/1000000.;
        node_lon = ((double)_location->getLongitude())/1000000.;
        node_altitude = ((double)_location->getAltitude()) / 1000.0;
      }
      _next_gps_update = millis() + (gps_update_interval_sec * 1000);
    }
  }
  #endif

public:
  double node_lat, node_lon;  // modify these, if you want to affect Advert location
  double node_altitude;       // altitude in meters

  SensorManager() { node_lat = 0; node_lon = 0; node_altitude = 0; }
  virtual bool begin() {
    #if ENV_INCLUDE_GPS
    detectLocationProvider();
    #endif
    return true;
  }

  virtual bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
    #if ENV_INCLUDE_GPS
    if (_location) {
      if ((requester_permissions & TELEM_PERM_LOCATION) && gps_active) {
        telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
      }
      return true;
    }
    #endif
    return false;
  }

  virtual void loop() {
    #if ENV_INCLUDE_GPS
    updateGpsCoordinates();
    #endif
  }

  virtual int getNumSettings() const {
    #if ENV_INCLUDE_GPS
    if (_location) return 1;
    #endif
    return 0;
  }

  virtual const char* getSettingName(int i) const {
    #if ENV_INCLUDE_GPS
    if (_location && i == 0) return "gps";
    #endif
    return NULL;
  }

  virtual const char* getSettingValue(int i) const {
    #if ENV_INCLUDE_GPS
    if (_location && i == 0) return gps_active ? "1" : "0";
    #endif
    return NULL;
  }

  virtual bool setSettingValue(const char* name, const char* value) {
    #if ENV_INCLUDE_GPS
    if (_location && strcmp(name, "gps") == 0) {
      if (strcmp(value, "0") == 0) {
        stop_gps();
      } else {
        start_gps();
      }
      return true;
    }
    if (strcmp(name, "gps_interval") == 0) {
      uint32_t interval_seconds = atoi(value);
      gps_update_interval_sec = (interval_seconds > 0) ? interval_seconds : 1;
      return true;
    }
    #endif
    return false;
  }

  #if ENV_INCLUDE_GPS
  virtual void start_gps() {
    if (_location && !gps_active) {
      gps_active = true;
      _location->begin();
    }
  }
  virtual void stop_gps() {
    if (_location && gps_active) {
      gps_active = false;
      _location->stop();
    }
  }

  LocationProvider* getLocationProvider() { return _location; }

  void registerLocationProvider(LocationProvider* loc) {
    if (_num_location_candidates < 4) {
      _location_candidates[_num_location_candidates++] = loc;
    }
  }
  #endif

  // Helper functions to manage setting by keys (useful in many places ...)
  const char* getSettingByKey(const char* key) {
    int num = getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(getSettingName(i), key) == 0) {
        return getSettingValue(i);
      }
    }
    return NULL;
  }
};
