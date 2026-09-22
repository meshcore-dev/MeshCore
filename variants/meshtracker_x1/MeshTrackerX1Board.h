#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class MeshTrackerX1Board : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;

public:
  MeshTrackerX1Board() : NRF52Board("X1_OTA") {}
  void begin();

  uint16_t getBattMilliVolts() override {
  #ifdef BATTERY_PIN
   #ifdef PIN_BAT_ADC_EN
    digitalWrite(PIN_BAT_ADC_EN, HIGH);
   #endif
    analogReference(AR_INTERNAL_3_0);
    analogReadResolution(12);
    delay(10);
    float volts = (analogRead(BATTERY_PIN) * ADC_MULTIPLIER * AREF_VOLTAGE) / 4096;

    analogReference(AR_DEFAULT);  // put back to default
    analogReadResolution(10);

    return volts * 1000;
  #else
    return 0;
  #endif
  }

  const char* getManufacturerName() const override {
    return "Seeed SenseCAP MeshTracker X1";
  }

  int buttonStateChanged() {
  #ifdef BUTTON_PIN
    uint8_t v = digitalRead(BUTTON_PIN);
    if (v != btn_prev_state) {
      btn_prev_state = v;
      return (v == USER_BTN_PRESSED) ? 1 : -1;
    }
  #endif
    return 0;
  }

  void shutdownPeripherals() override;
  void powerOff() override;
};
