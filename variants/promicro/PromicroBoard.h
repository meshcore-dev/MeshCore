#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#define  PIN_VBAT_READ 17
#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER (1.46484375f) // 160k BAT+ / 240k GND, 3.6V ADC full scale.
#endif
#ifndef PROMICRO_ADC_SAMPLE_TIME_US
  #define PROMICRO_ADC_SAMPLE_TIME_US 40
#endif

class PromicroBoard : public NRF52BoardDCDC {
protected:
  float adc_mult = ADC_MULTIPLIER;
#ifdef NRF52_POWER_MANAGEMENT
  uint32_t last_battery_check = 0;
  uint8_t low_battery_samples = 0;
#endif

public:
  PromicroBoard() : NRF52Board("ProMicro_OTA") {}
  void begin();
  void loop() override;

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReference(AR_DEFAULT);
    analogReadResolution(12);
    analogSampleTime(PROMICRO_ADC_SAMPLE_TIME_US);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;
    return (adc_mult * raw);
  }

  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;}
    else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override {
    if (adc_mult == 0.0f) {
      return ADC_MULTIPLIER;
    } else {
      return adc_mult;
    }
  }

  const char* getManufacturerName() const override {
    return "ProMicro DIY";
  }

  void powerOff() override;

  #ifdef NRF52_POWER_MANAGEMENT
    void initiateShutdown(uint8_t reason) override;
  #endif
};
