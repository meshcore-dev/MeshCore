#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>
#include "LoRaFEMControl.h"

class StationG3Board : public ESP32Board {
public:
  LoRaFEMControl loRaFEMControl;

  void begin() {
    ESP32Board::begin();
    loRaFEMControl.init();

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
    loRaFEMControl.setLNAEnable(enabled);
  }

  void setPrimaryPAHighPower(bool enabled) {
    loRaFEMControl.setPAGainEnable(enabled);
  }

  void onBeforeTransmit() override {
    ESP32Board::onBeforeTransmit();
    loRaFEMControl.setTxModeEnable();
  }

  void onAfterTransmit() override {
    ESP32Board::onAfterTransmit();
    loRaFEMControl.setRxModeEnable();
  }

  bool setLoRaFemLnaEnabled(bool enable) override;
  bool canControlLoRaFemLna() const override;
  bool isLoRaFemLnaEnabled() const override;
  bool setLoRaFemPaGainEnabled(bool enable) override;
  bool canControlLoRaFemPaGain() const override;
  bool isLoRaFemPaGainEnabled() const override;

  void powerOff() override;

  uint16_t getBattMilliVolts() override {
    return 0;
  }

  const char* getManufacturerName() const override {
    return "Station G3 ESP32";
  }
};
