#pragma once

#include <RadioLib.h>
#include <helpers/stm32/STM32Board.h>
#include <helpers/radiolib/CustomSTM32WLxWrapper.h>
#include <helpers/ArduinoHelpers.h>
#if ENV_INCLUDE_INA219
  #include <helpers/sensors/EnvironmentSensorManager.h>
#else
  #include <helpers/SensorManager.h>
#endif
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
#if ENV_INCLUDE_INA219
extern EnvironmentSensorManager sensors;
#else
extern SensorManager sensors;
#endif
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
