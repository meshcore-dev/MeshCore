#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class WioTrackerL1Board : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;
  bool _is_pa_enabled = false;
  KeyValueStore* _prefs = NULL;

  bool setLoRaFemPaGainEnabled(bool enable);
  bool isLoRaFemPaGainEnabled() const { return _is_pa_enabled; }

public:
  WioTrackerL1Board() : NRF52Board("WioTrackerL1 OTA") {}
  void begin();
  void attachDynamicPrefs(KeyValueStore* prefs);
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override;

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

  void powerOff() override {
    setLoRaFemPaGainEnabled(false);   // turn Off PA
    NRF52Board::powerOff();
  }
};
