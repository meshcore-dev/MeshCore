#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include <Adafruit_NeoPixel.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER      (0.5F)          // Even voltage divider on VBAT
#define VBAT_DIVIDER_COMP (2.0F)          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     (2)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class TechoCardBoard : public NRF52BoardDCDC {
  bool _torchStatus = false;
public:
  TechoCardBoard() : NRF52Board("TECHO_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;


  const char* getManufacturerName() const override {
    return "LilyGo T-Echo Card";
  }

  void powerOff() override;

  void toggleTorch();
  void turnOffLeds();
  
};
