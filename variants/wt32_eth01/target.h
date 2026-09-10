#pragma once

#include <RadioLib.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/SensorManager.h>

class WT32ETH01Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();
    setInhibitSleep(true);
  }
  const char* getManufacturerName() const override { return "WT32-ETH01 + HT-RA62"; }
};

extern WT32ETH01Board board;
extern WRAPPER_CLASS radio_driver;
extern ESP32RTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
