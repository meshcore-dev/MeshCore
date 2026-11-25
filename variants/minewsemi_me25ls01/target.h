#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <MinewsemiME25LS01Board.h>
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/NullDisplayDriver.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern MinewsemiME25LS01Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
