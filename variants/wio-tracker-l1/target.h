#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <WioTrackerL1Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
  #if defined(WIO_TRACKER_L1_EINK)
    #include <helpers/ui/GxEPDDisplay.h>
  #else
    #include <helpers/ui/SH1106Display.h>
  #endif
  #include <helpers/ui/MomentaryButton.h>
#endif
#include <helpers/sensors/EnvironmentSensorManager.h>

extern WioTrackerL1Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton joystick_left;
  extern MomentaryButton joystick_right;
  extern MomentaryButton back_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
