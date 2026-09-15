#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <TechoCardBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/U8g2Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern TechoCardBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton back_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
