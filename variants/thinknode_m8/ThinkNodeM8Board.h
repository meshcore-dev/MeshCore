#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.5859375F)   // 2.4V ADC range and 12-bit ADC resolution = 2400mV/4096

#define VBAT_DIVIDER      (0.57F)          // 150K + 150K voltage divider on VBAT
#define VBAT_DIVIDER_COMP (1.75F)          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     (4)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class ThinkNodeM8Board : public NRF52Board {
public:
  ThinkNodeM8Board() : NRF52Board("THINKNODE_M8_OTA") {}
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
    return "Elecrow ThinkNode-M8";
  }

  void shutdownPeripherals() override {
    // power off board
    NRF52Board::shutdownPeripherals();

    // make sure every gate is closed
    digitalWrite(DISP_EN, LOW);
    digitalWrite(PIN_PWR_EN, LOW);
    digitalWrite(PIN_GPS_EN, LOW);
    digitalWrite(SX126X_ANT_SW, LOW);
    digitalWrite(ADC_EN, LOW);

    #ifdef PIN_BUTTON1 // Use BTN to go out of sleep
    nrf_gpio_cfg_sense_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    #endif
  }
};
