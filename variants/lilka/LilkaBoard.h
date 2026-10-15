#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>

#ifndef PIN_VBAT_READ              
  #define  PIN_VBAT_READ    3
#endif
#ifndef ADC_MULTIPLIER            
  #define ADC_MULTIPLIER 1.33
#endif

class LilkaBoard : public ESP32Board {
protected:
  uint8_t btn_prev_state;

public:
  LilkaBoard() { }

  void begin();
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;
};
