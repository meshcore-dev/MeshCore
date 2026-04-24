#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include <helpers/ui/LEDManager.h>

#define ADC_FACTOR ((1000.0*ADC_MULTIPLIER*AREF_VOLTAGE)/ADC_MAX)

class ThinkNodeM3Board : public NRF52BoardDCDC {
protected:
#if NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif
  uint8_t btn_prev_state;

public:
  ThinkNodeM3Board() : NRF52Board("THINKNODE_M3_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  void onBeforeTransmit() override {
    if (ledManager) ledManager->onBeforeTransmit();
  }
  void onAfterTransmit() override {
    if (ledManager) ledManager->onAfterTransmit();
  }

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

  void powerOff() override {
    if (ledManager) ledManager->powerOff();

    // power off board
    sd_power_system_off();
  }
};
