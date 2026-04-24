#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include <helpers/ui/LEDManager.h>

#ifdef IKOKA_NRF52

class IkokaNrf52Board : public NRF52BoardDCDC {
public:
  IkokaNrf52Board() : NRF52Board("XIAO_NRF52_OTA") {}
  void begin();

  void onBeforeTransmit() override {
    if (ledManager) ledManager->onBeforeTransmit();
  }
  void onAfterTransmit() override {
    if (ledManager) ledManager->onAfterTransmit();
  }

  uint16_t getBattMilliVolts() override {
    // Please read befor going further ;)
    // https://wiki.seeedstudio.com/XIAO_BLE#q3-what-are-the-considerations-when-using-xiao-nrf52840-sense-for-battery-charging

    // We can't drive VBAT_ENABLE to HIGH as long
    // as we don't know wether we are charging or not ...
    // this is a 3mA loss (4/1500)
    digitalWrite(VBAT_ENABLE, LOW);
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(PIN_VBAT);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return "Ikoka Handheld E22 30dBm (Xiao_nrf52)";
  }
};

#endif
