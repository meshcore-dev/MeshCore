#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <RAK4631Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#if ENV_INCLUDE_WIND
  #include <helpers/sensors/WindSensor.h>
#endif

#ifdef DISPLAY_CLASS
  #include <helpers/ui/SSD1306Display.h>
  extern DISPLAY_CLASS display;
  #include <helpers/ui/MomentaryButton.h>
  extern MomentaryButton user_btn;
  #if defined(PIN_USER_BTN_ANA)
  extern MomentaryButton analog_btn;
  #endif
#endif

extern RAK4631Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
#if ENV_INCLUDE_WIND
extern WindSensor wind_sensor;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

