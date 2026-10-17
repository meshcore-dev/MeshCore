#pragma once

#define RADIOLIB_STATIC_ONLY 1

#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>

class NullSensorManager : public SensorManager {
public:
    bool begin() override {
        return true;
    }

    bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override {
        return true;
    }

    int getNumSettings() const override {
        return 0;
    }

    const char* getSettingName(int i) const override {
        return nullptr;
    }

    const char* getSettingValue(int i) const override {
        return nullptr;
    }

    bool setSettingValue(const char* name, const char* value) override {
        return false;
    }
};

extern ESP32Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern NullSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
