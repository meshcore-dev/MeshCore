#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include "TechoCardBoard.h"

#if ENV_INCLUDE_GPS
#include "GPSStreamCounter.h"
#endif

#ifdef DISPLAY_CLASS
  #if defined(USE_U8G2_DISPLAY)
    #include <helpers/ui/U8g2Display.h>
  #else
    #include <helpers/ui/SSD1306Display.h>
  #endif
  #include <helpers/ui/MomentaryButton.h>
#endif

extern TechoCardBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

extern EnvironmentSensorManager sensors;

#if ENV_INCLUDE_GPS
extern GPSStreamCounter gpsStream;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();