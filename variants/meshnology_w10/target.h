#pragma once

#include "MeshnologyW10Board.h"
#include "MeshnologyW10Radio.h"
#include "MeshnologyW10RTC.h"
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
#include <helpers/ui/ST7789LCDDisplay.h>
#include <helpers/ui/MomentaryButton.h>
#endif

extern MeshnologyW10Board board;
extern WRAPPER_CLASS radio_driver;
extern MeshnologyW10RTC rtc_clock;
extern EnvironmentSensorManager sensors;
#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
