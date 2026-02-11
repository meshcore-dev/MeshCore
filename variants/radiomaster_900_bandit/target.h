#pragma once

#define RADIOLIB_STATIC_ONLY 1
// #include <RadioLib.h>
#include <BanditBoard.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSX1276Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/AnalogJoystick.h>
#include <helpers/ui/SH1115Display.h>
#endif

extern BanditBoard board;

extern WRAPPER_CLASS radio_driver;
extern SensorManager sensors;
extern AutoDiscoverRTCClock rtc_clock;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
#if defined(PIN_USER_JOYSTICK)
extern AnalogJoystick analog_joystick;
#endif
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
