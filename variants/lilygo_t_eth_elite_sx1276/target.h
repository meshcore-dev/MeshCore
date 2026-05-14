#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/esp32/TEthEliteBoard.h>
#include <helpers/radiolib/CustomSX1276Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/ui/MomentaryButton.h>

extern MomentaryButton user_btn;

#ifdef PIN_USER_BTN_ANA
  extern MomentaryButton analog_btn;
#endif

#ifdef DISPLAY_CLASS
  #include <helpers/ui/SH1106Display.h>
  extern DISPLAY_CLASS display;
#endif

extern TEthEliteBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
