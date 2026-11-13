#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include "heltec_rcc6.h"

#include <RadioLib.h>
#include <helpers/ESP32Board.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#ifdef HELTEC_RCC6_WITH_DISPLAY
#include <helpers/ui/NV3001BDisplay.h>
#else
#include <helpers/ui/NullDisplayDriver.h>
#endif
#endif

extern HeltecRCC6Board board;
extern WRAPPER_CLASS radio_driver;
extern ESP32RTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
