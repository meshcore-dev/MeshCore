#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/esp32/ESPNOWRadio.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
#endif
#ifdef DISPLAY_CLASS
  #include "SCIndicatorDisplay.h"
  #include <helpers/ui/MomentaryButton.h>
#endif

extern ESP32Board board;
extern ESPNOWRadio radio_driver;
extern ESP32RTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
