#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
#define  PIN_VBAT_READ    4
#define  PIN_BAT_CTL      6
#define  MV_LSB   (3000.0F / 4096.0F) // 12-bit ADC with 3.0V input range

class T114Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  T114Board() : NRF52Board("T114_OTA") {}
  void begin();

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
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_BAT_CTL, OUTPUT);          // battery adc can be read only ctrl pin 6 set to high
    digitalWrite(PIN_BAT_CTL, 1);

    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    digitalWrite(6, 0);

    return (uint16_t)((float)adcvalue * MV_LSB * 4.9);
  }

  const char* getManufacturerName() const override {
    return "Heltec T114";
  }

  void powerOff() override {
#ifdef LED_PIN
    digitalWrite(LED_PIN, HIGH);
#endif
#if ENV_INCLUDE_GPS == 1
    pinMode(GPS_EN, OUTPUT);
    digitalWrite(GPS_EN, LOW);
#endif
    sd_power_system_off();
  }
};
