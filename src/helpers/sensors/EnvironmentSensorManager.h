#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

#ifdef GPS_GUARD
  // GPS anti-spoofing (opt-in). Optional region box: GPS_GUARD_LAT_MIN/MAX, GPS_GUARD_LON_MIN/MAX.
  #ifndef GPS_GUARD_MIN_SATS
    #define GPS_GUARD_MIN_SATS        4
  #endif
  #ifndef GPS_GUARD_MAX_HDOP10
    #define GPS_GUARD_MAX_HDOP10      40      // HDOP 4.0
  #endif
  #ifndef GPS_GUARD_FENCE_M
    #define GPS_GUARD_FENCE_M         1000    // max distance from the reference position (0 = off)
  #endif
  #ifndef GPS_GUARD_STABLE_FIXES
    #define GPS_GUARD_STABLE_FIXES    5       // consecutive consistent fixes before trusting
  #endif
  #ifndef GPS_GUARD_STABLE_M
    #define GPS_GUARD_STABLE_M        100
  #endif
  #ifndef GPS_GUARD_MAX_SPEED_KMH
    #define GPS_GUARD_MAX_SPEED_KMH   250     // jump limit relative to the last trusted fix
  #endif
  #ifndef GPS_GUARD_TIME_MAX_STEP
    #define GPS_GUARD_TIME_MAX_STEP   600     // max GPS-vs-clock correction (s) once the clock is valid
  #endif
  #ifndef GPS_GUARD_TIME_DRIFT_BASE
    #define GPS_GUARD_TIME_DRIFT_BASE 5       // after the first sync: fixed allowance (s) ...
  #endif
  #ifndef GPS_GUARD_TIME_DRIFT_PPM
    #define GPS_GUARD_TIME_DRIFT_PPM  100     // ... plus clock drift since that sync (~8.6 s/day)
  #endif
  #ifndef GPS_GUARD_ALERT_MIN
    #define GPS_GUARD_ALERT_MIN       60      // alert after GPS disagrees with the clock this long ...
  #endif
  #ifndef GPS_GUARD_ALERT_TOLERANCE
    #define GPS_GUARD_ALERT_TOLERANCE 60      // ... by an offset that stays within this many seconds
  #endif
#endif

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t   sub_channel;
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;

  bool     gps_detected = false;
  bool     gps_active = false;
  uint32_t gps_update_interval_sec = 1;

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif

  #ifdef GPS_GUARD
  // Only plausible, stable fixes update the position, and GPS time is written to
  // the clock only while such a fix is current.
  mutable char _setting_buf[72];
  double   _gps_ref_lat = 0, _gps_ref_lon = 0;   // 0,0 = no reference position
  uint32_t _gps_fence_m = GPS_GUARD_FENCE_M;
  bool     _have_trusted_fix = false;     // node_lat/lon hold a trusted position
  unsigned long _last_trusted_ms = 0;
  double   _last_trusted_lat = 0, _last_trusted_lon = 0;
  double   _cand_lat = 0, _cand_lon = 0;
  uint8_t  _stable_count = 0;
  char     _fix_status[24] = "no fix";
  char     _time_status[24] = "-";
  bool     _time_accepted = false;        // a GPS time sync was accepted (drift budget anchor)
  unsigned long _last_time_accept_ms = 0;
  // Sustained, consistent GPS-vs-clock disagreement (rejected syncs) raises an alert.
  bool     _time_disagree = false;
  long     _time_disagree_offset = 0;     // GPS minus clock, seconds
  unsigned long _time_disagree_since_ms = 0;
  bool     _time_alert = false;
  void evaluateGpsFix();
  bool checkGpsTime(uint32_t gps_time, uint32_t clock_time);
  void trackTimeDisagreement(long offset);
  static bool timeGuardCallback(void* ctx, uint32_t gps_time, uint32_t clock_time);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  bool isGPSDetected() const override { return gps_detected; }
  #else
  EnvironmentSensorManager(){};
  #endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  #if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
  void loop() override;
  #endif
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
