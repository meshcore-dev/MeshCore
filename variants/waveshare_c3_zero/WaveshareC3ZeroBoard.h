#pragma once

#include <helpers/ESP32Board.h>
#include <Arduino.h>

#include <driver/uart.h>

class WaveshareC3ZeroBoard : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_gpio_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

  #if defined(LORA_TX_BOOST_PIN)
      gpio_hold_dis((gpio_num_t) LORA_TX_BOOST_PIN);
      gpio_deep_sleep_hold_dis();
  #endif
    }

  #ifdef LORA_TX_BOOST_PIN
    pinMode(LORA_TX_BOOST_PIN, OUTPUT);
    digitalWrite(LORA_TX_BOOST_PIN, HIGH);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif
  }

#if defined(LORA_TX_BOOST_PIN) || defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
  #if defined(P_LORA_TX_LED)
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  #endif
  #if defined(LORA_TX_BOOST_PIN)
    digitalWrite(LORA_TX_BOOST_PIN, LOW);
    delay(5);
  #endif
  }
  void onAfterTransmit() override {
  #if defined(LORA_TX_BOOST_PIN)
    digitalWrite(LORA_TX_BOOST_PIN, HIGH);
  #endif
  #if defined(P_LORA_TX_LED)
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  #endif
  }
#endif

  uint16_t getBattMilliVolts() override {
    return 0;  // no battery-voltage-divider circuit on this board
  }

  const char* getManufacturerName() const override {
    return "Waveshare ESP32-C3-Zero";
  }
};
