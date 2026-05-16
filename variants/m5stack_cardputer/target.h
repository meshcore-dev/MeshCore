#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <M5CardputerBoard.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#ifdef HAS_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
#endif
#ifdef DISPLAY_CLASS
  #include <helpers/ui/M5CardputerDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

#ifdef HAS_GPS
class CardputerSensorManager : public SensorManager {
  bool gps_active = false;
  LocationProvider* _location;
  void* _node_prefs;  // NodePrefs pointer for state sync

  void start_gps();
  void stop_gps();
public:
  CardputerSensorManager(LocationProvider &location): _location(&location), _node_prefs(nullptr) { }
  void setNodePrefs(void* prefs) { _node_prefs = prefs; }
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
#endif

extern M5CardputerBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
#ifdef HAS_GPS
extern CardputerSensorManager sensors;
#else
extern SensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
