#pragma once

#ifdef SIM_BUILD
  // Emulator build (e.g. Wokwi): no SX1262 hardware — use the no-op SimRadio so
  // the firmware boots and runs WiFi/MQTT/CLI/display. See src/helpers/sim/.
  #include <helpers/sim/SimRadio.h>
  #include <HeltecV3Board.h>
#else
  #define RADIOLIB_STATIC_ONLY 1
  #include <RadioLib.h>
  #include <helpers/radiolib/RadioLibWrappers.h>
  #include <HeltecV3Board.h>
  #include <helpers/radiolib/CustomSX1262Wrapper.h>
#endif
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SSD1306Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern HeltecV3Board board;
#ifdef SIM_BUILD
  extern SimRadio radio_driver;
#else
  extern WRAPPER_CLASS radio_driver;
#endif
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
