#pragma once

#include <MeshCore.h>
#include <Arduino.h>

class WioTrackerL1Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  uint8_t btn_prev_state;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

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

  void reboot() override {
    NVIC_SystemReset();
  }

  void powerOff() override {
    sd_power_system_off();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
