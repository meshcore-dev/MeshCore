#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/CustomSX1262Wrapper.h>
#include "MeshPocket.h"
#include <helpers/RadioLibWrappers.h>
#include <helpers/SensorManager.h>

extern HeltecMeshPocket board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;


bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();

class MeshPocketSensorManager : public SensorManager {
public:
  MeshPocketSensorManager() {};
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry);
  void loop();
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};

extern MeshPocketSensorManager sensors;

#include "helpers/ui/SSD1680Display.h"
extern SSD1680Display display;