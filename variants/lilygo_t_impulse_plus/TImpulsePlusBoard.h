#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class TImpulsePlusBoard : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;

public:
  TImpulsePlusBoard() : NRF52Board("T-Impulse-Plus OTA") {}
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
    // enable battery voltage measurement
    digitalWrite(BATTERY_MEASUREMENT_CONTROL, HIGH);
    delay(100);

    analogReadResolution(12);
    analogReference(AR_INTERNAL);
    delay(10);

    // read battery voltage
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(BATTERY_ADC_DATA);

    // disable battery voltage measurement
    digitalWrite(BATTERY_MEASUREMENT_CONTROL, LOW);

    return ((adcvalue * ((3000.0 / 4096.0)))) * 2.0;
  }

  const char* getManufacturerName() const override {
    return "LilyGo T-Impulse-Plus";
  }

  void powerOff() override {

    // turn off 3.3v
    digitalWrite(RT9080_EN, LOW);

    // power off system
    sd_power_system_off();

  }
};
