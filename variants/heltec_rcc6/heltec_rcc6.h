#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

#ifndef ADC_MULTIPLIER
#define ADC_MULTIPLIER 4.95f
#endif

class HeltecRCC6Board : public ESP32Board {
  float adcMultiplier = ADC_MULTIPLIER;

public:
  void begin();
  void powerOff() override;
  uint16_t getBattMilliVolts() override;

  bool setAdcMultiplier(float multiplier) override {
    adcMultiplier = multiplier == 0.0f ? ADC_MULTIPLIER : multiplier;
    return true;
  }

  float getAdcMultiplier() const override { return adcMultiplier; }
  const char *getManufacturerName() const override { return "heltec_rcc6"; }
};
