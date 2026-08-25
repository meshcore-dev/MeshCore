#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>

#ifndef P_PRIMARY_LNA_EN_ACTIVE
#define P_PRIMARY_LNA_EN_ACTIVE LOW
#endif

#ifndef P_PA1_EN_ACTIVE
#define P_PA1_EN_ACTIVE HIGH
#endif

class StationG3Board : public ESP32Board {
  void setPAModeHigh(bool enabled) {
#ifdef P_PA1_EN
    // Station G3 PA PL1 mode: LOW/open is PA low, HIGH/short is PA high.
    digitalWrite(P_PA1_EN, enabled ? P_PA1_EN_ACTIVE : !P_PA1_EN_ACTIVE);
#endif
  }

  void setPrimaryLNAControl(bool enabled) {
#ifdef P_PRIMARY_LNA_EN
    // Station G3 primary LNA mode is active-low: LOW/open is LNA on, HIGH/short is LNA off.
    digitalWrite(P_PRIMARY_LNA_EN, enabled ? P_PRIMARY_LNA_EN_ACTIVE : !P_PRIMARY_LNA_EN_ACTIVE);
#endif
  }

public:
  void begin() {
    ESP32Board::begin();

#ifdef P_PA1_EN
    rtc_gpio_hold_dis((gpio_num_t)P_PA1_EN);
    pinMode(P_PA1_EN, OUTPUT);
    setPAModeHigh(false);
#endif

#ifdef P_PRIMARY_LNA_EN
    rtc_gpio_hold_dis((gpio_num_t)P_PRIMARY_LNA_EN);
    pinMode(P_PRIMARY_LNA_EN, OUTPUT);
    setPrimaryLNAControl(true);
#endif

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      uint64_t wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1ULL << P_LORA_DIO_1)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void setPrimaryLNAEnable(bool enabled) {
    setPrimaryLNAControl(enabled);
  }

  void setPrimaryPAHighPower(bool enabled) {
    setPAModeHigh(enabled);
  }

  void onBeforeTransmit() override {
    ESP32Board::onBeforeTransmit();
    setPrimaryLNAControl(false);
  }

  void onAfterTransmit() override {
    ESP32Board::onAfterTransmit();
    setPrimaryLNAControl(true);
  }

  void powerOff() override;

  uint16_t getBattMilliVolts() override {
    return 0;
  }

  const char* getManufacturerName() const override {
    return "Station G3 ESP32";
  }
};
