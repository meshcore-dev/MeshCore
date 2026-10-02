#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <HeltecRC52Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#if defined(UI_HAS_ROTARY_INPUT)
#include <helpers/ui/RotaryInput.h>
#endif
#ifdef HELTEC_RC52_WITH_DISPLAY
#include <helpers/ui/NV3001BDisplay.h>
#else
#include <helpers/ui/NullDisplayDriver.h>
#endif
#endif

extern HeltecRC52Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#if defined(UI_HAS_ROTARY_INPUT)
extern RotaryInput& rotary_input;
#endif
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
