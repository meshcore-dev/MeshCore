#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ThinkNodeM6Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/GxEPDDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

// Wraps the stock environment sensor manager purely to drive the status LED.
// Its loop() is already called every iteration by every example sketch, and it
// is the only place holding the live GNSS state, so it is where the board gets
// told whether to blink once (no fix) or twice (fix).
#ifdef ENV_INCLUDE_GPS
class ThinkNodeM6SensorManager : public EnvironmentSensorManager {
public:
  ThinkNodeM6SensorManager(LocationProvider& location) : EnvironmentSensorManager(location) { }
  void loop() override;
};
#else
class ThinkNodeM6SensorManager : public EnvironmentSensorManager {
public:
  ThinkNodeM6SensorManager() : EnvironmentSensorManager() { }
  void loop() override;
};
#endif

extern ThinkNodeM6Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern ThinkNodeM6SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

