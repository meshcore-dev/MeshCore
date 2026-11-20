#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ThinknodeG3Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>

extern ThinknodeG3Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
