#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// The Muzi Base PCB ships in two radio flavors. Keep the user-facing identity
// (OTA/DFU name and reported manufacturer) distinct per radio even though the
// board logic is shared.
#if defined(USE_SX1262)
  #define MUZI_BASE_OTA_NAME  "MUZI_BASE_UNO_OTA"
  #define MUZI_BASE_MFR_NAME  "Muzi Base Uno"
#else
  #define MUZI_BASE_OTA_NAME  "MUZI_BASE_DUO_OTA"
  #define MUZI_BASE_MFR_NAME  "Muzi Base Duo"
#endif

class muzi_baseBoard : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  muzi_baseBoard() : NRF52Board(MUZI_BASE_OTA_NAME) {}
  void begin();

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(1);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    // ADC_MULTIPLIER is the voltage divider ratio
    return (raw * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return MUZI_BASE_MFR_NAME;
  }
};
