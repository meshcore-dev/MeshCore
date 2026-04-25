#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>
#include <PCA9557.h>

extern PCA9557 expander;

class ThinknodeM5Board : public ESP32Board {

public:

  void begin();
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override ;

  void onBeforeTransmit() override {
    expander.digitalWrite(EXP_PIN_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    expander.digitalWrite(EXP_PIN_LED, LOW);   // turn TX LED off
  }
};