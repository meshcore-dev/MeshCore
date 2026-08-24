#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>

// From Meshtastic Station G1 variant.h
#ifndef BATTERY_PIN
  #define BATTERY_PIN 35
#endif

#ifndef BATTERY_SENSE_SAMPLES
  #define BATTERY_SENSE_SAMPLES 30
#endif

#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER 6.45f
#endif

class StationG1Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    Serial.begin(115200);
    delay(1000);
    Serial.println("booting station g1 meshcore");

    // Battery ADC setup
    analogReadResolution(12);
    analogSetPinAttenuation((uint8_t)BATTERY_PIN, ADC_11db);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1) {
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
    uint32_t acc_mv = 0;
    for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
      acc_mv += (uint32_t)analogReadMilliVolts((uint8_t)BATTERY_PIN);
      delay(2);
    }

    const float pin_mv = (float)acc_mv / (float)BATTERY_SENSE_SAMPLES;
    const float batt_mv = pin_mv * ADC_MULTIPLIER;

    if (batt_mv < 0.0f) {
      return 0;
    }
    if (batt_mv > 65535.0f) {
      return 65535;
    }
    return (uint16_t)(batt_mv + 0.5f);
  }

  const char* getManufacturerName() const override {
    return "Station G1";
  }
};

