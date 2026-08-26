#pragma once

#include <Arduino.h>

// LoRa radio module pins for Meshadventurer
#define  P_LORA_DIO_1   33
#define  P_LORA_NSS     18
#define  P_LORA_RESET   23
#define  P_LORA_BUSY    32
#define  P_LORA_SCLK     5
#define  P_LORA_MISO    19
#define  P_LORA_MOSI    27

#define  PIN_VBAT_READ    35

#include "ESP32Board.h"

class MeshadventurerBoard : public ESP32Board {

public:
  void begin() {
    ESP32Board::begin();

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

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  }

  const char* getManufacturerName() const override {
    return "Meshadventurer";
  }
};
