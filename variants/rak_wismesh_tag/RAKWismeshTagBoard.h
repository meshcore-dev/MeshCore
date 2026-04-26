#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
#define  PIN_VBAT_READ    5
#define  ADC_MULTIPLIER   (3 * 1.73 * 1.187 * 1000)

class RAKWismeshTagBoard : public NRF52BoardDCDC {
public:
  RAKWismeshTagBoard() : NRF52Board("WISMESHTAG_OTA") {}
  void begin();

#if defined(P_LORA_TX_LED) && defined(LED_STATE_ON)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LED_STATE_ON);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, !LED_STATE_ON);   // turn TX LED off
  }
#endif

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
    return "RAK WisMesh Tag";
  }

  void powerOff() override {
    #ifdef BUZZER_EN
        digitalWrite(BUZZER_EN, LOW);
    #endif

    #ifdef PIN_GPS_EN
        digitalWrite(PIN_GPS_EN, LOW);
    #endif

    // set led on and wait for button release before poweroff
    #ifdef LED_PIN
    digitalWrite(LED_PIN, HIGH);
    #endif
    #ifdef BUTTON_PIN
    // wismesh tag uses LOW to indicate button is pressed, wait until it goes HIGH to indicate it was released
    while(digitalRead(BUTTON_PIN) == LOW);
    #endif
    #ifdef LED_GREEN
    digitalWrite(LED_GREEN, LOW);
    #endif
    #ifdef LED_BLUE
    digitalWrite(LED_BLUE, LOW);
    #endif

    #ifdef BUTTON_PIN
    // configure button press to wake up when in powered off state
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(BUTTON_PIN), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    #endif

    sd_power_system_off();
  }
};
