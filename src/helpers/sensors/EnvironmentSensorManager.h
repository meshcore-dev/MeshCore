#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

// Not for RAK WisBlock GPS (same condition as RAK_WISBLOCK_GPS in the .cpp), which has its own power handling
#if ENV_INCLUDE_GPS && !(defined(RAK_BOARD) && !defined(RAK_WISMESH_TAG))
  #define ENV_GPS_DUTY_CYCLE 1
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
  // Duty cycling: with gps_duty_secs > 0, power the GPS down after each fix and wake it
  // gps_duty_secs later. gps_active (the user setting) stays true while powered down.
  // Only exposed as a setting once the app calls configureGPSDuty(), since it must persist it.
  uint32_t      gps_duty_secs = 0;
  bool          gps_duty_enabled = false;
  mutable char  gps_duty_val[12];
  bool          gps_powered = false;
  unsigned long gps_next_wake = 0;
  unsigned long gps_powered_at = 0;
  unsigned long gps_fix_since = 0;
  void power_on_gps();
  void dutyCycleGPS();
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  bool isGPSDetected() const override { return gps_detected; }
  #if ENV_GPS_DUTY_CYCLE
  // Call after begin(). Lists gps_duty and applies the saved value (invalid = always on).
  void configureGPSDuty(uint32_t secs);
  #endif
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
