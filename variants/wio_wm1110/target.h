#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "WioWM1110Board.h"
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

extern WioWM1110Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();

