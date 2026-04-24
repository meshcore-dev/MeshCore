#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include <helpers/ui/LEDManager.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER_COMP ADC_MULTIPLIER          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     BATTERY_PIN
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class ThinkNodeM6Board : public NRF52BoardDCDC {
protected:
#if NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  ThinkNodeM6Board() : NRF52Board("THINKNODE_M6_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  void onBeforeTransmit() override {
    if (ledManager) ledManager->onBeforeTransmit();
  }
  void onAfterTransmit() override {
    if (ledManager) ledManager->onAfterTransmit();
  }

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode M6";
  }

  void powerOff() override {
    if (ledManager) ledManager->powerOff();

    // power off board
    sd_power_system_off();
  }
};
