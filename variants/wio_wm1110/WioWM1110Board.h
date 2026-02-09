#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#ifdef WIO_WM1110

#ifdef Serial
  #undef Serial
#endif
#define Serial Serial1

class WioWM1110Board : public NRF52BoardDCDC {
public:
  WioWM1110Board() : NRF52Board("WM1110_OTA") {}
  void begin();

#if defined(LED_GREEN)
  void onBeforeTransmit() override {
    digitalWrite(LED_RED, HIGH);
  }
  void onAfterTransmit() override {
    digitalWrite(LED_RED, LOW);
  }
#endif

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(BATTERY_PIN);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE * 1000.0) / 4096.0;
  }

  const char* getManufacturerName() const override {
    return "Seeed Wio WM1110";
  }

  void enableSensorPower(bool enable) {
    digitalWrite(SENSOR_POWER_PIN, enable ? HIGH : LOW);
    if (enable) {
      delay(100);
    }
  }
};

#endif

