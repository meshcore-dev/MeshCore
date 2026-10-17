#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
// Battery sense defaults to the WisBlock base board divider (VBAT -> 1M/1.5M -> AIN0/P0.05).
// Both defines can be overridden from build flags to read an externally supplied battery
// voltage on another analog pin, e.g. the USB-C SBU pin of a Voltaic V25/V50/V75 pack
// (1/2 of its cell voltage) wired to the base board J11 AIN1 pin:
//   -D PIN_VBAT_READ=31 -D ADC_MULTIPLIER=7200
// ADC_MULTIPLIER is the reported millivolts at ADC full scale (3.6V): 3600 reports the
// pin voltage as-is, 7200 doubles it (for 1/2-scale sources like the V25 SBU pin).
#ifndef PIN_VBAT_READ
  #define  PIN_VBAT_READ    5
#endif
#ifndef ADC_MULTIPLIER
  #define  ADC_MULTIPLIER   (3 * 1.73 * 1.187 * 1000)
#endif

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

  // TX/RX switching is handled by SX1262 DIO2 -> SKY66122 CTX (hardware-timed).
  // No onBeforeTransmit/onAfterTransmit overrides needed.
};
