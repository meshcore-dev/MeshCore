#pragma once

#include "esp_mac.h"

#include <helpers/ESP32Board.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/LocationProvider.h>

// GPS
#define HAS_GPS         1

// BATTERY
#define BATTERY_PIN     0
#define BATTERY_DIVIDER 2.43f

//--------------------
// SEIR VERSION 5
//--------------------
class SEIRv5Board : public ESP32Board {
public:
  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    // Set attenuation BEFORE reading
    analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

    // Dummy read to force ADC init on ESP32-C6
    analogRead(BATTERY_PIN);

    // Average samples
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
      sum += analogRead(BATTERY_PIN);
    }
    uint16_t raw = sum / 4;

    float v_adc = (raw / 4095.0f) * 3.3f;
    float v_bat = v_adc * BATTERY_DIVIDER;
    uint16_t mv = (uint16_t)(v_bat * 1000.0f);
    return mv;
  }

  const char *getManufacturerName() const { return "OROBAS"; }

  void reboot() override { ESP.restart(); }
};

/* ---------- Sensor Manager ---------- */
class SEIRSensorManager : public SensorManager {
  LocationProvider *_loc;
  bool gps_active = true;

public:
  SEIRSensorManager(LocationProvider &loc) : _loc(&loc) {}

  bool begin() override { return true; }
  void loop() override {
    if (!gps_active) return;

    _loc->loop();

    static uint32_t lastPrint = 0;
    uint32_t now = millis();
  }

  bool querySensors(uint8_t requester_permissions, CayenneLPP &telemetry) override {
    int next_available_channel = TELEM_CHANNEL_SELF + 1;

    // Location
    if ((requester_permissions & TELEM_PERM_LOCATION)) {
      if (_loc && _loc->isValid()) {
        telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
      }
    }
    return true;
  }

  LocationProvider *getLocationProvider() override { return _loc; }

  int getNumSettings() const override { return 1; }

  const char *getSettingName(int i) const override { return (i == 0) ? "gps" : nullptr; }

  const char *getSettingValue(int i) const override { return (i == 0 && gps_active) ? "1" : "0"; }

  bool setSettingValue(const char *name, const char *value) override {
    if (strcmp(name, "gps") == 0) {
      gps_active = strcmp(value, "0") != 0;
      return true;
    }
    return false;
  }
};

/* ---------- Globals ---------- */
extern LocationProvider &location;
extern SEIRSensorManager sensors;

extern SEIRv5Board board;
extern CustomSX1262Wrapper radio_driver;
extern ESP32RTCClock rtc_clock;

/* ---------- Radio ---------- */
bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
