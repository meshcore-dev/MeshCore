#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>

class HeltecE290Board : public ESP32Board {

public:
  RefCountedDigitalPin periph_power;

  HeltecE290Board() : periph_power(PIN_VEXT_EN) { }

  void begin();
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override ;

};
