#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

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

  #if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
  #endif

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode-M1";
  }

  void powerOff() override {

    // turn off all leds, sd_power_system_off will not do this for us
    #ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, LOW);
    #endif

    // power off board
    sd_power_system_off();

  }
};
