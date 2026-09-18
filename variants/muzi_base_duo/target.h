#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "muzi_base_duoBoard.h"
#include <helpers/radiolib/CustomLR1121Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/EnvironmentSensorManager.h>  // Added: Include for EnvironmentSensorManager
#include <helpers/ui/MomentaryButton.h>


#ifdef muzi_base_duo_superIO
  #include <helpers/ui/SH1107Display.h>
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton joystick_left;
  extern MomentaryButton joystick_right;
  extern MomentaryButton joystick_up;
  extern MomentaryButton joystick_down;
  extern MomentaryButton back_btn;
#else
  #include "helpers/ui/NullDisplayDriver.h"
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;

#endif

extern muzi_base_duoBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
