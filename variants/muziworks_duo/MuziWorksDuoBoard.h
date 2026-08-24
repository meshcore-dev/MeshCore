#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#ifdef MUZIWORKS_DUO

#define PIN_VBAT_READ       BATTERY_PIN
#define BATTERY_SAMPLES     8

class MuziWorksDuoBoard : public NRF52BoardDCDC {
public:
  MuziWorksDuoBoard() : NRF52Board("MuziWorksDuo_OTA") {}
  void begin();

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;
    return (ADC_MULTIPLIER * raw);
  }

  const char* getManufacturerName() const override {
    return "MuziWorks Duo";
  }

#if defined(LED_GREEN)
  void onBeforeTransmit() override {
    digitalWrite(LED_GREEN, LED_STATE_ON);
  }
  void onAfterTransmit() override {
    digitalWrite(LED_GREEN, !LED_STATE_ON);
  }
#endif
};

#endif
