#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#define  PIN_VBAT_READ    31
// Voltage divider ratio 1.667 calibrated by simon-muzi (Meshtastic PR #8716)
// Adapted for MeshCore's default 3.6V analog reference
#define  ADC_MULTIPLIER   (3.6 * 1.667 * 1000)

class R1NeoBoard : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  R1NeoBoard() : NRF52Board("R1NEO_OTA") {}
  void begin();

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
  }

  const char* getManufacturerName() const override {
    return "muzi works R1 Neo";
  }
};
