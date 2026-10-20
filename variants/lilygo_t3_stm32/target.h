#pragma once

#include <RadioLib.h>
#include <helpers/stm32/STM32Board.h>
#include <helpers/radiolib/CustomSTM32WLxWrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/ui/SSD1306Display.h>
#include <helpers/ui/MomentaryButton.h>

class LilyGoT3STM32Board : public STM32Board {
public:
  void begin() override;

  const char* getManufacturerName() const override {
    return "LilyGO T3 STM32";
  }
};

extern LilyGoT3STM32Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern SensorManager sensors;
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
