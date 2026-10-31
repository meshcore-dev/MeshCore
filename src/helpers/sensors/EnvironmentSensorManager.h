#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

// Not for RAK WisBlock GPS (same condition as RAK_WISBLOCK_GPS in the .cpp), which probes sockets instead
#if ENV_INCLUDE_GPS && !(defined(RAK_BOARD) && !defined(RAK_WISMESH_TAG))
  #define ENV_GPS_PIN_CONFIG 1
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
  #if ENV_GPS_PIN_CONFIG
  // Runtime overrides of PIN_GPS_RX/TX/EN and GPS_BAUD_RATE (-1 / 0 = variant default).
  // Only exposed as settings once the app calls configureGPS(), since it must persist them.
  bool     gps_cfg_enabled = false;
  int8_t   gps_pin_rx = -1;
  int8_t   gps_pin_tx = -1;
  int8_t   gps_pin_en = -1;
  uint32_t gps_baud = 0;
  mutable char gps_cfg_val[4][12];
  bool isGPSPinAllowed(int pin, bool output) const;
  #endif
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
  #if ENV_GPS_PIN_CONFIG
  // Call before begin(). Invalid values fall back to the variant defaults.
  void configureGPS(int rx, int tx, int en, uint32_t baud);
  // Current overrides (-1 / 0 = board default), for persisting after a setting change.
  void getGPSOverrides(int8_t& rx, int8_t& tx, int8_t& en, uint32_t& baud) const {
    rx = gps_pin_rx; tx = gps_pin_tx; en = gps_pin_en; baud = gps_baud;
  }
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
