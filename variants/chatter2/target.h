#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/ESP32Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include "Chatter2Radio.h"
#include <helpers/radiolib/CustomLLCC68Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/ui/MomentaryButton.h>
#include "Chatter2Board.h"
#include "Chatter2Display.h"

extern Chatter2Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
