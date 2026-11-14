#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <ChallengerRP2040LoraBoard.h>
#include <RadioLib.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSX1276Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>

extern ChallengerRP2040LoraBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
