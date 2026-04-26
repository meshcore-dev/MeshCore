#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// built-ins
#define  PIN_VBAT_READ    29
#define  PIN_BAT_CTL      34
#define  MV_LSB   (3000.0F / 4096.0F) // 12-bit ADC with 3.0V input range

class HeltecMeshPocket : public NRF52BoardDCDC {
public:
  HeltecMeshPocket() : NRF52Board("MESH_POCKET_OTA") {}
  void begin();

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_BAT_CTL, OUTPUT);          // battery adc can be read only ctrl pin set to high
    pinMode(PIN_VBAT_READ, INPUT);
    digitalWrite(PIN_BAT_CTL, HIGH);

    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    digitalWrite(PIN_BAT_CTL, LOW);

    return (uint16_t)((float)adcvalue * MV_LSB * 4.9);
  }

  const char* getManufacturerName() const override {
    return "Heltec MeshPocket";
  }

  void powerOff() override {
    sd_power_system_off();
  }
};
