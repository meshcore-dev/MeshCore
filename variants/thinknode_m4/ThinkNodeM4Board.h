#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

class ThinkNodeM4Board : public NRF52BoardDCDC {
protected:
#if NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif
  uint8_t btn_prev_state;
  HardwareSerial * battery_serial;
  uint16_t bat_level_mv = 0;
  uint8_t bat_level_percent =0;

public:
  ThinkNodeM4Board() : NRF52Board("THINKNODE_M3_OTA"), battery_serial(&Serial2) {}
  void begin();
  uint16_t getBattMilliVolts() override;

#ifdef P_LORA_TX_LED
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LED_STATE_ON);  // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, !LED_STATE_ON); // turn TX LED off
  }
#endif

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode M4";
  }

  int buttonStateChanged() {
  #ifdef BUTTON_PIN
    uint8_t v = digitalRead(BUTTON_PIN);
    if (v != btn_prev_state) {
      btn_prev_state = v;
      return (v == USER_BTN_PRESSED) ? 1 : -1;
    }
  #endif
    return 0;
  }

  void shutdownPeripherals() override {
    // shutdown common peripherals
    NRF52Board::shutdownPeripherals();

    #ifdef LED_STATUS
    digitalWrite(LED_STATUS, HIGH);
    #endif
    #ifdef BUTTON_PIN
    while(!digitalRead(BUTTON_PIN));
    #endif
    #ifdef LED_STATUS
    digitalWrite(LED_STATUS, LOW);
    #endif

    digitalWrite(LED_BAT1, LOW);
    digitalWrite(LED_BAT2, LOW);
    digitalWrite(LED_BAT3, LOW);
    digitalWrite(LED_BAT4, LOW);
    digitalWrite(LED_STATUS, LOW);
    digitalWrite(LED_PIN, LOW);

    digitalWrite(PIN_PWR_EN, LOW);
    digitalWrite(I2C_POWER, !I2C_POWER_ACTIVE);
    digitalWrite(PIN_GPS_POWER, !GPS_POWER_ACTIVE);

    #ifdef BUTTON_PIN
    nrf_gpio_cfg_sense_input(BUTTON_PIN, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);
    #endif
  }
};
