#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <HeltecTowerV2Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/ExternalWatchdogManager.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#include "helpers/ui/NullDisplayDriver.h"
#endif

class TowerV2ExternalWatchdog : public ExternalWatchdogManager {
public:
    TowerV2ExternalWatchdog() {}
    bool begin() override;
    void loop() override;
    unsigned long getIntervalMs() const override;
    void feed() override;
};

extern HeltecTowerV2Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
extern TowerV2ExternalWatchdog external_watchdog;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
