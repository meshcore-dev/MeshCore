#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <MeshtinyBoard.h>
#include <RadioLib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#include <helpers/ui/SSD1306Display.h>
#endif
#include <helpers/sensors/EnvironmentSensorManager.h>

extern MeshtinyBoard board;
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
