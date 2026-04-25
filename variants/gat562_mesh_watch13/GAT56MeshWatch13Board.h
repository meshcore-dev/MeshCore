#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>


class GAT56MeshWatch13Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  GAT56MeshWatch13Board() : NRF52Board("GAT562_OTA") {}
  void begin();

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
  }

  const char* getManufacturerName() const override {
    return "GAT562 Mesh Watch 13";
  }


  void powerOff() override {
    uint32_t button_pin = PIN_BUTTON1;
    nrf_gpio_cfg_input(button_pin, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_sense_set(button_pin, NRF_GPIO_PIN_SENSE_LOW);
    sd_power_system_off(); 
  }

};
