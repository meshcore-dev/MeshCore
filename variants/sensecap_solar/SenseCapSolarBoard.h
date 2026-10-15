#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class SenseCapSolarBoard : public NRF52BoardDCDC {
protected:
  KeyValueStore* _prefs = NULL;
  bool _leds_off = false;
  bool _leds_applied = false;

  void setLedsEnabled(bool enable);

#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  SenseCapSolarBoard() : NRF52Board("SENSECAP_SOLAR_OTA") {}
  void begin();
  void attachDynamicPrefs(KeyValueStore* prefs);
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override;
  void loop() override;

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    if (!_leds_off) digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
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
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_BLUE, LOW);

#ifdef PIN_USER_BTN
    while (digitalRead(PIN_USER_BTN) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_USER_BTN]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#elif defined(PIN_BUTTON1)
    while (digitalRead(PIN_BUTTON1) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_BUTTON1]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
    initiateShutdown(SHUTDOWN_REASON_USER);
#else
    NRF52Board::powerOff();
#endif
  }
};
