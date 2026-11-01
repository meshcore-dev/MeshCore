#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
//#include <helpers/ESP32Board.h>
#include <ThinknodeM5Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/GxEPDDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern ThinknodeM5Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
#ifdef ENV_INCLUDE_GPS
// The GPS switch is only an input: the firmware turns the GPS on and off to follow it.
class ThinknodeM5SensorManager : public EnvironmentSensorManager {
  int last_switch_state = -1;  // the first check applies the switch, after the app setting
  unsigned long last_switch_check = 0;
public:
  using EnvironmentSensorManager::EnvironmentSensorManager;
  void loop() override;
};
extern ThinknodeM5SensorManager sensors;
#else
extern EnvironmentSensorManager sensors;
#endif
extern PCA9557 expander;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton backlight_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();


 