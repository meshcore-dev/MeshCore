#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include <helpers/ui/LEDManager.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER      (0.5F)          // 150K + 150K voltage divider on VBAT
#define VBAT_DIVIDER_COMP (2.0F)          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     (4)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class ThinkNodeM1Board : public NRF52Board {
public:
  ThinkNodeM1Board() : NRF52Board("THINKNODE_M1_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  void onBeforeTransmit() override {
    if (ledManager) ledManager->onBeforeTransmit();
  }
  void onAfterTransmit() override {
    if (ledManager) ledManager->onAfterTransmit();
  }

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode-M1";
  }

  void powerOff() override {
    if (ledManager) ledManager->powerOff();

    // power off board
    sd_power_system_off();
  }
};
