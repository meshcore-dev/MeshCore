#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class WioTrackerL1Board : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;
  bool _is_pa_enabled = false;

public:
  WioTrackerL1Board() : NRF52Board("WioTrackerL1 OTA") {}
  void begin();

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL);
    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return "Seeed Wio Tracker L1";
  }

  bool canControlLoRaFemPaGain() const override { return true; }
  bool setLoRaFemPaGainEnabled(bool enable) override;
  bool isLoRaFemPaGainEnabled() const override { return _is_pa_enabled; }

  void powerOff() override {
    setLoRaFemPaGainEnabled(false);   // turn Off PA
    NRF52Board::powerOff();
  }
};
