#pragma once

#include <helpers/ESP32Board.h>
#include <Arduino.h>

#include <driver/rtc_io.h>
#include <driver/uart.h>

class NiceRFLR2021Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_gpio_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
    }

#ifdef PIN_VBAT_READ
    pinMode(PIN_VBAT_READ, INPUT);
#endif
  }

  void enterDeepSleep(uint32_t secs, int8_t wake_pin = -1) {
    gpio_set_direction(gpio_num_t(P_LORA_DIO_1), GPIO_MODE_INPUT);
    if (wake_pin >= 0) {
      gpio_set_direction((gpio_num_t)wake_pin, GPIO_MODE_INPUT);
    }

    gpio_deep_sleep_hold_en();
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    if (wake_pin >= 0) {
      esp_deep_sleep_enable_gpio_wakeup(
          (1 << P_LORA_DIO_1) | (1 << wake_pin), ESP_GPIO_WAKEUP_GPIO_HIGH);
    } else {
      esp_deep_sleep_enable_gpio_wakeup(1 << P_LORA_DIO_1,
                                        ESP_GPIO_WAKEUP_GPIO_HIGH);
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    esp_deep_sleep_start();
  }

  uint16_t getBattMilliVolts() override {
#ifdef PIN_VBAT_READ
    analogReadResolution(12);
    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;
    return (2 * raw);
#else
    return 0;
#endif
  }

  const char *getManufacturerName() const override {
    return "NiceRF LR2021";
  }
};
