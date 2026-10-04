#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <LilkaBoard.h>
#ifdef USE_SX1262
  #include <helpers/radiolib/CustomSX1262Wrapper.h>
#elif defined(USE_SX1268)
  #include <helpers/radiolib/CustomSX1268Wrapper.h>
#endif
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/ST7789LCDDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern LilkaBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton joystick_left;
  extern MomentaryButton joystick_right;
  extern MomentaryButton back_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
