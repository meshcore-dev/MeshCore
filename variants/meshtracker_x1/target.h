#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "MeshTrackerX1Board.h"
#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <Adafruit_SPA06_003.h>
#ifdef DISPLAY_CLASS
  #include "NullDisplayDriver.h"
#endif

class MeshTrackerX1SensorManager: public SensorManager {
  bool gps_active = false;
  bool baro_ok = false;
  Adafruit_SPA06_003 spa06;
  LocationProvider * _nmea;

  void start_gps();
  void sleep_gps();
  void stop_gps();
public:
  MeshTrackerX1SensorManager(LocationProvider &nmea): _nmea(&nmea) { }
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
  LocationProvider* getLocationProvider() { return _nmea; }
};

#ifdef DISPLAY_CLASS
  extern NullDisplayDriver display;
#endif

extern MeshTrackerX1Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern MeshTrackerX1SensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
