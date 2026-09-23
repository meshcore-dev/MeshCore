#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <MMMBoard.h>

#if defined(USE_LR2021)
  #include <helpers/radiolib/CustomLR2021Wrapper.h>
#elif defined(USE_SX1262)
  #include <helpers/radiolib/CustomSX1262Wrapper.h>
#endif

#include <helpers/AutoDiscoverRTCClock.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SSD1306Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

#include <helpers/sensors/EnvironmentSensorManager.h>

extern MMMBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
