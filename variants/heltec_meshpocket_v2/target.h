#pragma once

#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/ui/MomentaryButton.h>
#include <helpers/ui/NV3001BDisplay.h>
#include <UITask.h>
#include "HeltecMeshPocketV2Board.h"

extern HeltecMeshPocketV2Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
extern UITask ui_task;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
