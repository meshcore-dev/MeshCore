#pragma once

#include <MeshCore.h>
#include <Arduino.h>

// built-ins
#define  PIN_VBAT_READ    4
#define  PIN_BAT_CTL      6
#define  MV_LSB   (3000.0F / 4096.0F) // 12-bit ADC with 3.0V input range

class T114Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;

    NRF_SAADC->ENABLE = 1;

    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_BAT_CTL, OUTPUT);          // battery adc can be read only ctrl pin 6 set to high
    digitalWrite(PIN_BAT_CTL, 1);

    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    digitalWrite(6, 0);

    NRF_SAADC->ENABLE = 0;

    return (uint16_t)((float)adcvalue * MV_LSB * 4.9);
  }

  const char* getManufacturerName() const override {
    return "Heltec T114";
  }

  void reboot() override {
    NVIC_SystemReset();
  }

  void powerOff() override {
    sd_power_system_off();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
