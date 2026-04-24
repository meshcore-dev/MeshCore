#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include <helpers/ui/LEDManager.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#ifdef XIAO_NRF52

class XiaoNrf52Board : public NRF52BoardDCDC {
protected:
#if NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  XiaoNrf52Board() : NRF52Board("XIAO_NRF52_OTA") {}
  void begin();

  void onBeforeTransmit() override {
    if (ledManager) ledManager->onBeforeTransmit();
  }
  void onAfterTransmit() override {
    if (ledManager) ledManager->onAfterTransmit();
  }

  uint16_t getBattMilliVolts() override;

  const char* getManufacturerName() const override {
    return "Seeed Xiao-nrf52";
  }

  void powerOff() override {
    // set led on and wait for button release before poweroff
    digitalWrite(PIN_LED, LOW);
#ifdef PIN_USER_BTN
    while(digitalRead(PIN_USER_BTN) == USER_BTN_PRESSED);
#endif

    if (ledManager) ledManager->powerOff();
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(PIN_LED, HIGH);

#ifdef PIN_USER_BTN
    // configure button press to wake up when in powered off state
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_USER_BTN]), NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);
#endif

    sd_power_system_off();
  }
};

#endif