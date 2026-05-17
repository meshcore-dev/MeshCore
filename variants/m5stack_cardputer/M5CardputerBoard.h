#pragma once

#include <Wire.h>
#include <Arduino.h>
#include <M5Cardputer.h>
#include "helpers/ESP32Board.h"

#ifndef PIN_VBAT_READ
#define PIN_VBAT_READ 10
#endif

#define BATTERY_SAMPLES 8

class M5CardputerBoard : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.internal_imu = false;
    cfg.internal_rtc = true;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    M5Cardputer.begin(cfg, true);
    delay(100);
    M5Cardputer.Keyboard.begin();

    // Cardputer ADV LoRa Cap RF switch supply is controlled by the PI4IOE expander.
    // Only expander pin 0 should be driven high here; SX1262 DIO2 handles TX/RX RF switching.
    M5.In_I2C.writeRegister8(0x43, 0x03, 0xFE, 100000);
    delay(10);
    M5.In_I2C.writeRegister8(0x43, 0x01, 0x01, 100000);
    delay(200);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      uint64_t wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1ULL << P_LORA_DIO_1)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);
    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
      delay(1);
    }
    raw = raw / BATTERY_SAMPLES;
    return (2 * raw);
  #else
    return 0;
  #endif
  }

  const char* getManufacturerName() const override {
    return "M5Stack Cardputer-Adv";
  }

  void powerOff() override {
    M5.Display.sleep();
  #ifdef PIN_USER_BTN
    enterDeepSleep(0, PIN_USER_BTN);
  #else
    enterDeepSleep(0, -1);
  #endif
  }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    uint64_t wake_mask = (1ULL << P_LORA_DIO_1);
    if (pin_wake_btn >= 0) {
      wake_mask |= (1ULL << pin_wake_btn);
    }
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
    }

    esp_deep_sleep_start();
  }
};
