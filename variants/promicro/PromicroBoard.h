#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

#define  PIN_VBAT_READ 17
#define  ADC_MULTIPLIER   (1.815f) // dependent on voltage divider resistors. TODO: more accurate battery tracking

class PromicroBoard : public NRF52BoardDCDC {
protected:
  float adc_mult = ADC_MULTIPLIER;

public:
  PromicroBoard() : NRF52Board("ProMicro_OTA") {}
  void begin();

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
  #if ENV_INCLUDE_INA3221 && defined(TELEM_INA3221_BAT_CH)
    uint16_t mv;
    if (ina3221ReadBattery(&mv, NULL)) return mv;   // falls back to the ADC until the INA3221 is up
  #endif
    analogReadResolution(12);

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

#if ENV_INCLUDE_INA3221 && defined(TELEM_INA3221_BAT_CH)
  bool isChargerActive() override { return ina3221BatteryCharging(); }
#endif

  const char* getManufacturerName() const override {
    return "ProMicro DIY";
  }
};
