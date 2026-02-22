#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ThinkNodeM1Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#if ENV_INCLUDE_GPS
#include <helpers/sensors/LocationProvider.h>
#endif
#ifdef DISPLAY_CLASS
  #include <helpers/ui/GxEPDDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

class ThinkNodeM1SensorManager : public SensorManager {
  #if ENV_INCLUDE_GPS
  bool last_gps_switch_state = false;
  #endif
public:
  ThinkNodeM1SensorManager() {}
  bool begin() override;
  #if ENV_INCLUDE_GPS
  void loop() override;
  #endif
};

extern ThinkNodeM1Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern ThinkNodeM1SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
