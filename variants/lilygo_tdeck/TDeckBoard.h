#pragma once

#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

#define PIN_VBAT_READ 4
#define BATTERY_SAMPLES 8
#define ADC_MULTIPLIER (2.0f * 3.3f * 1000)

class TDeckBoard : public ESP32Board {
public:
  void begin();

  #ifdef P_LORA_TX_LED
    void onBeforeTransmit() override{
      digitalWrite(P_LORA_TX_LED, LOW); // turn TX LED on - invert pin for SX1276
    }

    void onAfterTransmit() override{
      digitalWrite(P_LORA_TX_LED, HIGH); // turn TX LED off - invert pin for SX1276
    }
  #endif

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH); // wake up on: recv LoRa packet
    } else {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH); // wake up on: recv LoRa packet OR wake btn
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start(); // CPU halts here and never returns!
  }

  uint16_t getBattMilliVolts() {
    #if defined(PIN_VBAT_READ) && defined(ADC_MULTIPLIER)
      analogReadResolution(12);

      uint32_t raw = 0;
      for (int i = 0; i < BATTERY_SAMPLES; i++) {
        raw += analogRead(PIN_VBAT_READ);
      }

      raw = raw / BATTERY_SAMPLES;
      return (ADC_MULTIPLIER * raw) / 4096;
    #else
      return 0;
    #endif
  }

  const char* getManufacturerName() const{
    return "LilyGo T-Deck";
  }
};