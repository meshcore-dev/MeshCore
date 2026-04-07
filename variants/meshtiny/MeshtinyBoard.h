#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

class MeshtinyBoard : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;

public:
  MeshtinyBoard() : NRF52Board("Meshtiny OTA") {}
  void begin();

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char *getManufacturerName() const override { return "Meshtiny"; }

  void reboot() override { NVIC_SystemReset(); }

  void powerOff() override {

#ifdef PIN_USER_BTN
    while (digitalRead(PIN_USER_BTN) == LOW) {
      delay(10);
    }
#endif

#ifdef PIN_3V3_EN
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, LOW);
#endif


#ifdef PIN_LED1
    digitalWrite(PIN_LED1, LOW);
#endif

#ifdef PIN_LED2
    digitalWrite(PIN_LED2, LOW);
#endif

#ifdef PIN_USER_BTN
    nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_USER_BTN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

    sd_power_system_off();
  }

};
