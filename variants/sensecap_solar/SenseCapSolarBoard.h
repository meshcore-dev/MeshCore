#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class SenseCapSolarBoard : public NRF52BoardOTA {
public:
  SenseCapSolarBoard() : NRF52BoardOTA("SENSECAP_SOLAR_OTA") {}
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
    digitalWrite(VBAT_ENABLE, LOW);
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(BATTERY_PIN);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return "Seeed SenseCap Solar";
  }

  void powerOff() override {
#ifdef GPS_EN
    digitalWrite(GPS_EN, LOW);
#endif

#ifdef LED_GREEN
    digitalWrite(LED_GREEN, LOW);
#endif
#ifdef LED_BLUE
    digitalWrite(LED_BLUE, LOW);
#endif

#ifdef PIN_BUTTON1
    while(digitalRead(PIN_BUTTON1) == LOW);
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_BUTTON1]), NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);
#endif

    sd_power_system_off();
  }
};
