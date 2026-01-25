#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ThinkNodeM1Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/GxEPDDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

class ThinkNodeM1SensorManager : public SensorManager {
  bool gps_active = false;
  bool last_gps_switch_state = false;
  LocationProvider* _location;

  void start_gps();
  void stop_gps();
public:
  ThinkNodeM1SensorManager(LocationProvider &location): _location(&location) { }
  LocationProvider* getLocationProvider() override { return _location; }
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
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
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
