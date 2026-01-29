#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

#define ADC_FACTOR ((1000.0*ADC_MULTIPLIER*AREF_VOLTAGE)/ADC_MAX)

class ThinknodeM3Board : public Nrf52BoardDCDC {
protected:
  uint8_t btn_prev_state;

public:
  void begin();

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;

    analogReference(AR_INTERNAL_2_4);
    analogReadResolution(ADC_RESOLUTION);
    delay(10);

    // ADC range is 0..2400mV and resolution is 12-bit (0..4095)
    adcvalue = analogRead(PIN_VBAT_READ);
    // Convert the raw value to compensated mv, taking the resistor-
    // divider into account (providing the actual LIPO voltage)
    return (uint16_t)((float)adcvalue * ADC_FACTOR);
  }

#if defined(P_LORA_TX_LED)
#if !defined(P_LORA_TX_LED_ON)
#define P_LORA_TX_LED_ON HIGH
#endif
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, P_LORA_TX_LED_ON);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, !P_LORA_TX_LED_ON);   // turn TX LED off
  }
  #endif

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode M3";
  }

  int buttonStateChanged() {
  #ifdef BUTTON_PIN
    uint8_t v = digitalRead(BUTTON_PIN);
    if (v != btn_prev_state) {
      btn_prev_state = v;
      return (v == LOW) ? 1 : -1;
    }
  #endif
    return 0;
  }

  void powerOff() override { sd_power_system_off(); }
};