#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

#if ENV_INCLUDE_INA3221 && defined(TELEM_INA3221_BAT_CH)
  #if TELEM_INA3221_BAT_CH < 1 || TELEM_INA3221_BAT_CH > 3
    #error "TELEM_INA3221_BAT_CH must be 1, 2 or 3"
  #endif
  #ifndef TELEM_INA3221_CHARGE_THRESHOLD_A
    #define TELEM_INA3221_CHARGE_THRESHOLD_A 0.005f  // ignore current noise below 5 mA
  #endif
  // Battery monitored on INA3221 channel TELEM_INA3221_BAT_CH (1..3), for boards that take
  // their battery voltage / charging state from it. Returns false until the INA3221 is found.
  bool ina3221ReadBattery(uint16_t* millivolts, float* amps);

  // Positive battery-channel current means charging, unless the shunt is wired the other
  // way round (TELEM_INA3221_CHARGING_INVERT).
  inline bool ina3221BatteryCharging() {
    float amps;
    if (!ina3221ReadBattery(NULL, &amps)) return false;
  #ifdef TELEM_INA3221_CHARGING_INVERT
    amps = -amps;
  #endif
    return amps > TELEM_INA3221_CHARGE_THRESHOLD_A;
  }
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
