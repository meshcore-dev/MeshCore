#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <WioTrackerL1Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ArduinoHelpers.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SH1106Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif
#include <helpers/sensors/EnvironmentSensorManager.h>

class WioTrackerL1SensorManager : public SensorManager
{
  bool gps_active = false;
  LocationProvider *_location;

  void start_gps();
  void stop_gps();

public:
  WioTrackerL1SensorManager(LocationProvider &location) : _location(&location) {}
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP &telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char *getSettingName(int i) const override;
  const char *getSettingValue(int i) const override;
  bool setSettingValue(const char *name, const char *value) override;
};


extern WioTrackerL1Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern WioTrackerL1SensorManager sensors;
#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton joystick_left;
  extern MomentaryButton joystick_right;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
