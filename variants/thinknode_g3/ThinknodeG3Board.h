#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "soc/rtc_cntl_reg.h"

#define PIN_ANTENNA_SW 45
#define PIN_LINK_LED   5
#define PIN_LORA_LED   6

extern CustomSX1262Wrapper radio_driver;

class ThinknodeG3Board : public ESP32Board {
  unsigned long _lora_led_off_at = 0;
  uint32_t _last_rx_cnt = 0;
  uint32_t _last_tx_cnt = 0;

public:
  void begin() {
    // Enable SX1262 RF switch / antenna power (stock firmware drives GPIO45 HIGH at init)
    pinMode(PIN_ANTENNA_SW, OUTPUT);
    digitalWrite(PIN_ANTENNA_SW, HIGH);

    ESP32Board::begin();

    // Active-LOW front panel LEDs (LOW = ON, HIGH = OFF)
    pinMode(PIN_LINK_LED, OUTPUT);
    digitalWrite(PIN_LINK_LED, LOW);   // LINK LED ON

    pinMode(PIN_LORA_LED, OUTPUT);
    digitalWrite(PIN_LORA_LED, HIGH);  // LoRa activity LED OFF when idle
  }

  void triggerLoRaLed(unsigned long duration_ms = 120) {
    digitalWrite(PIN_LORA_LED, LOW);   // Active-LOW: ON
    unsigned long target = millis() + duration_ms;
    if (target > _lora_led_off_at) {
      _lora_led_off_at = target;
    }
  }

  void onBeforeTransmit() override {
    triggerLoRaLed(150);
  }

  void onAfterTransmit() override {
    triggerLoRaLed(80);
  }

  void loop() override {
    unsigned long now = millis();

    // 1. LoRa LED (GPIO 6): flash on any RX preamble/packet or TX packet
    uint32_t rx_now = radio_driver.getPacketsRecv();
    uint32_t tx_now = radio_driver.getPacketsSent();
    if (rx_now != _last_rx_cnt || tx_now != _last_tx_cnt || radio_driver.isReceivingPacket()) {
      _last_rx_cnt = rx_now;
      _last_tx_cnt = tx_now;
      triggerLoRaLed(120);
    } else if (_lora_led_off_at != 0 && (long)(now - _lora_led_off_at) >= 0) {
      digitalWrite(PIN_LORA_LED, HIGH); // Active-LOW: OFF
      _lora_led_off_at = 0;
    }

    // 2. LINK LED (GPIO 5): Solid ON when Wi-Fi is connected (or disabled), 500ms blink while connecting
    if (WiFi.getMode() == WIFI_OFF || WiFi.status() == WL_CONNECTED) {
      digitalWrite(PIN_LINK_LED, LOW);  // Active-LOW: Solid ON
    } else {
      digitalWrite(PIN_LINK_LED, ((now / 500) % 2) ? LOW : HIGH);
    }
  }

  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override {
    if (strcmp(command, "bootloader") == 0 || strcmp(command, "dfu") == 0) {
      REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
      esp_restart();
      return true;
    }
    return false;
  }

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode G3";
  }
};
