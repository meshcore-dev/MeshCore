#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "muzi_baseBoard.h"
#if defined(USE_LR1121)
  #include <helpers/radiolib/CustomLR1121Wrapper.h>
#elif defined(USE_SX1262)
  #include <helpers/radiolib/CustomSX1262Wrapper.h>
#else
  #error "muzi_base: no radio selected (define USE_LR1121 or USE_SX1262)"
#endif
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/EnvironmentSensorManager.h>  // Added: Include for EnvironmentSensorManager
#include <helpers/ui/MomentaryButton.h>


#ifdef muzi_base_superIO
  #include <helpers/ui/SH1107Display.h>
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton joystick_left;
  extern MomentaryButton joystick_right;
  extern MomentaryButton back_btn;
#elif defined(DISPLAY_CLASS)
  #include "helpers/ui/NullDisplayDriver.h"
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

extern muzi_baseBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;

// muzi sensor manager, polled from main loop via sensors.loop():
//  - user button: USER_BTN_POWER_OFF_CLICKS quick presses -> power off (all builds)
//  - gps mode switch on/off (superIO only, see thinknode_m1)
class MuziBaseSensorManager : public EnvironmentSensorManager {
  unsigned long _pwr_last_click = 0;
  uint8_t _pwr_clicks = 0;
#if ENV_INCLUDE_GPS
  int _last_gps_sw = -1;
  unsigned long _next_sw_check = 0;
#endif
public:
#if ENV_INCLUDE_GPS
  MuziBaseSensorManager(LocationProvider& location) : EnvironmentSensorManager(location) {}
#else
  MuziBaseSensorManager() {}
#endif
  bool begin() override;
  void loop() override;
};
extern MuziBaseSensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
