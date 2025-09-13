#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include "nano-g2.h"

#include <RadioLib.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#include <helpers/ui/SH1106Display.h>
#endif
#include <helpers/sensors/LocationProvider.h>

class NanoG2UltraSensorManager : public SensorManager {
  bool gps_active = false;
  LocationProvider *_location;

  void start_gps();
  void stop_gps();

public:
  NanoG2UltraSensorManager(LocationProvider &location) : _location(&location) {}
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP &telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char *getSettingName(int i) const override;
  const char *getSettingValue(int i) const override;
  bool setSettingValue(const char *name, const char *value) override;
};

extern NanoG2Ultra board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern NanoG2UltraSensorManager sensors;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
