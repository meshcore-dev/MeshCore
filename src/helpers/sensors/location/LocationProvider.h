#pragma once

#include "GpsDriver.h"
#include <Mesh.h>

#ifndef GPS_CYCLE_INTERVAL_MS
  #define GPS_CYCLE_INTERVAL_MS 300000  // 5 minutes default
#endif

#ifndef GPS_WAKE_TIMEOUT_MS
  #define GPS_WAKE_TIMEOUT_MS 30000     // 30 seconds default
#endif

class LocationProvider {
private:
  GpsDriver* _driver = nullptr;

  // Configuration
  bool _enabled = false;
  uint32_t _cycle_interval_ms = GPS_CYCLE_INTERVAL_MS;
  uint32_t _wake_timeout_ms = GPS_WAKE_TIMEOUT_MS;
  uint8_t _min_satellites = 4;

  // State
  bool _detected = false;
  bool _hardware_on = false;
  long _next_cycle = 0;
  long _wake_time = 0;
  bool _reading_phase = false;

public:
  double node_lat = 0;
  double node_lon = 0;
  double node_altitude = 0;
  long node_timestamp = 0;

  static GpsDriver* detectDriver();

  LocationProvider(GpsDriver* driver) : _driver(driver) {}

  bool begin();
  void loop();

  void setEnabled(bool enabled) {
    _enabled = enabled;
    if (!_enabled && _hardware_on) {
      stopHardware();
    }
  }

  void setCycleInterval(uint32_t interval_ms) {
    _cycle_interval_ms = interval_ms;
  }

  void setWakeTimeout(uint32_t timeout_ms) {
    _wake_timeout_ms = timeout_ms;
  }

  void setMinSatellites(uint8_t min_sats) {
    _min_satellites = min_sats;
  }

  bool isDetected() const { return _detected; }
  bool isEnabled() const { return _enabled; }
  bool isHardwareOn() const { return _hardware_on; }
  bool hasValidFix() const;
  int getSatelliteCount() const { return _driver ? _driver->satellitesCount() : 0; }

  GpsDriver* getDriver() { return _driver; }

  void startHardware();
  void stopHardware();
  bool updateLocation();
  void syncTime();  // Force immediate GPS sync

private:
  bool validateReading(long lat, long lon, int sats) const;
};
