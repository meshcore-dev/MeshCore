#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <MeshSolarBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/ST7789Display.h>
#endif




extern MeshSolarBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
