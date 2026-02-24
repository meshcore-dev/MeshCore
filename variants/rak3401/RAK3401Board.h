#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
#define  PIN_VBAT_READ    5
#define  ADC_MULTIPLIER   (3 * 1.73 * 1.187 * 1000)

#define PIN_3V3_EN (34)
#define WB_IO2 PIN_3V3_EN

class RAK3401Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif
public:
  RAK3401Board() : NRF52Board("RAK3401_OTA") {}
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
    return "RAK 3401";
  }

  // SKY66122 FEM TX/RX switching via CTX pin.
  // CTX=HIGH: TX mode + 5V boost ON (PA powered from VCC1/VCC2)
  // CTX=LOW:  RX mode + 5V boost OFF (LNA powered from VSUP1 at 3.3V)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_PA_EN, HIGH);  // CTX=1: TX mode, boost on
  }

  void onAfterTransmit() override {
    digitalWrite(P_LORA_PA_EN, LOW);   // CTX=0: RX mode, boost off
  }
};
