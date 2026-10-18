#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <../heltec_v3/HeltecV3Board.h>
class HeltecWirelessPaperBoard : public HeltecV3Board {
public:
  const char* getManufacturerName() const override {
    return "Heltec Wireless Paper";
  }
};
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#ifdef DISPLAY_CLASS
#include <helpers/ui/E213Display.h>
#include <helpers/ui/MomentaryButton.h>
#endif

extern HeltecWirelessPaperBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
