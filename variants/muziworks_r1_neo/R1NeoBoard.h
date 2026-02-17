#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include "NullDisplayDriver.h"
#include "MomentaryButton.h"

#define DISPLAY_CLASS NullDisplayDriver

class R1NeoBoard : public NRF52BoardDCDC, public NRF52BoardOTA {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  R1NeoBoard() : NRF52BoardOTA("R1NEO_OTA") {}
  void begin();

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    #if defined(LED_BLUE)
       // turn off that annoying blue LED before transmitting
       digitalWrite(LED_BLUE, LOW);
    #endif
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    #if defined(LED_BLUE)
       // do it after transmitting too, just in case
       digitalWrite(LED_BLUE, LOW);
    #endif
  }
#endif

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
