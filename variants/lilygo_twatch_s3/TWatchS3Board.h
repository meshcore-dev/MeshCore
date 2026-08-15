#pragma once

// Pin mappings must be defined before including ESP32Board.h so begin() brings up
// Wire on the correct I2C pins and sleep() can reference P_LORA_DIO_1.

// Main I2C bus (AXP2101 PMU, PCF8563 RTC)
#define PIN_BOARD_SDA   10
#define PIN_BOARD_SCL   11
#define I2C_PMU_ADD     0x34

#ifndef PIN_PMU_IRQ
  #define PIN_PMU_IRQ   21
#endif

#include <Wire.h>
#include <Arduino.h>
#include "XPowersLib.h"
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

class TWatchS3Board : public ESP32Board {
  XPowersLibInterface* PMU = NULL;

  bool power_init();

public:
  void begin();

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
    } else {
      esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    esp_deep_sleep_start();
  }

  uint16_t getBattMilliVolts() override {
    return PMU ? PMU->getBattVoltage() : 0;
  }

  const char* getManufacturerName() const override {
    return "LilyGo T-Watch S3";
  }
};
